# CheerLights BLE profile

This page specifies how a phone sends the CheerLights color to a light over Bluetooth Low Energy.
This firmware implements the profile, and the CheerLights app for Android speaks it.
A light that follows this page works with the app.

This page describes protocol version 1.
The UUIDs below are fixed for version 1.
Test vectors for every format are in [ble-vectors.json](ble-vectors.json).
Lights and phones that follow the [compatibility rules](#compatibility) work together whichever is newer.

## Roles

The light is the GATT peripheral and the phone is the central.
An iPhone cannot put a color in an advertisement, because Apple's advertising API carries only a local name and service UUIDs.
The phone therefore connects and writes the color.

## Advertising

The light advertises the service UUID in its primary advertisement, plus a name of up to 8 characters.
It uses a static address, because Android's companion device presence does not support random addresses.
This firmware names itself `CL-` and four hex digits from its address, such as `CL-6B70`.

## GATT

| Item | UUID | Properties | Content |
|---|---|---|---|
| CheerLights service | `b73a0a04-bdb7-4051-9135-815aac2abaa3` | | |
| `Color` | `0fc3891d-11a7-408c-9283-b14dffc5bcff` | Read, write, write without response | The frame below |
| `Info` | `3554cfbc-0bcc-47f2-9af2-3b313c97a59d` | Read | The five bytes below |
| `Light` | `0a8345b7-afa9-47df-9d63-e98130514e19` | Read, write, write without response | Brightness, effect, and speed, below |
| `Effects` | `c4fa31c4-e81e-49d2-be0c-530c01865776` | Read | The animations the light has, below |

`Info` is byte 0 protocol version, bytes 1 to 3 firmware major, minor, and patch, and byte 4 capability flags.
Flag bit 0 means the light has a display, bit 1 means it has a physical off control, and bit 2 means it has `Light` and `Effects`.
A phone shows brightness and effect controls only when bit 2 is set, so a light without them still works with every phone.
Bit 3 means the light is not set up: it does not know where its LEDs are, so it shows nothing.
A phone that sees bit 3 tells its owner to set the light up over USB, on the [install page](https://cheerlights.github.io/cheerlights-esp32-ble/).
Firmware before 0.4.0 never sets bit 3.

## Frame

The frame is ten bytes, which fit the 20-byte default ATT payload and the 13 bytes available for 128-bit service data.
Multi-byte fields are little-endian.

| Byte | Field | Meaning |
|---|---|---|
| 0 | Version and flags | High nibble is the protocol version (1), low nibble is flags, and bit 0 means stale |
| 1 | Palette index | 0 to 10 from [colors.json](colors.json), or `0xFF` when the color is outside the palette |
| 2 to 4 | R, G, B | The color as published |
| 5 to 6 | Sequence | `u16`, increases on each change and wraps |
| 7 to 8 | Age | `u16`, minutes since the color was set, saturating at 65535 |
| 9 | Reserved | Zero |

Age is relative because a light has no real-time clock.
Stale means the phone has lost the feed, so the color may be old.
A receiver ignores a frame shorter than ten bytes or with a version it does not know.
It reads the first ten bytes of a longer frame and ignores flag bits it does not know.
A read of `Color` returns the last frame the light accepted.

## Light

`Light` is four bytes, written by the phone and read back on connect, so the phone's controls start from what the light shows.
The light keeps the last value across restarts.

| Byte | Field | Meaning |
|---|---|---|
| 0 | Version | 1 |
| 1 | Brightness | 1 to 255, a share of the light's own brightness cap; 0 is treated as 1 |
| 2 | Effect | An id from `Effects`; 0 is solid, and an unknown id shows solid |
| 3 | Speed | 1 to 255, slow to fast, for effects that move; 0 is treated as 1 |

Brightness is relative, so each light decides how bright 255 is, such as a low cap on a board that runs hot.
The physical off control (bit 1) stays the way to turn the light off, and `Light` cannot turn it back on.
Effects that show the color animate the color from the latest frame.
A stale frame is always shown as stale, without an effect, so the wearer can trust it.
A receiver ignores a `Light` value shorter than four bytes or with an unknown version, and reads the first four bytes of a longer one.

## Effects

`Effects` is a list the phone reads to show the light's animations by name, so firmware can add effects without a phone update.

| Bytes | Field | Meaning |
|---|---|---|
| 0 | Version | 3 |
| 1 | Count | Number of entries |
| then, per entry | Size, id, flags, length, name | `u8` size of the rest of the entry, `u8` id, `u8` flags, `u8` name length up to 20, then a UTF-8 name |

Entry 0 is always solid.
Flag bit 0 means the effect shows the color it is sent, such as by dimming or sparkling it.
An effect without it draws its own colors, such as a rainbow.
A phone following the CheerLights color offers only effects with bit 0 set, and a receiver ignores flag bits it does not know.
A receiver reads the fields of an entry it knows and moves to the next entry by its size, so a later firmware can add a field to each entry.
It ignores bytes after the last entry.
The value can exceed one ATT payload, so the phone reads it with a long read.

## Compatibility

A phone's app updates from the app store, and a light's firmware updates only when someone flashes it, so a phone often meets a light that is older or newer than it.
These rules keep every pair working:

- Values only grow.
  A new field goes after the existing ones, and a receiver ignores bytes past the fields it knows.
- A field never changes meaning.
  If one ever had to, the change would get a new characteristic, and lights would keep the old one for older phones.
- Receivers ignore flag bits and capability bits they do not know, and a light shows solid for an effect id it does not have.
- A light that receives a shorter value than it knows, but at least the length on this page, keeps its current setting for the fields not sent.
  A phone that reads a shorter value than it knows treats the missing fields as absent.
- A new feature gets a new characteristic or an `Info` capability bit, and a phone offers the feature only on a light that has it.

The vectors named `...Newer` in [ble-vectors.json](ble-vectors.json) are values from a later writer, and every receiver must read the fields it knows from them.

## Security

Writes are unauthenticated, so anyone nearby can repaint a light.
That is acceptable only because every characteristic controls light and nothing else: color, brightness, and the light's own animations.
Never add a command beyond light over Bluetooth, such as network settings, firmware updates, or anything that stores data from the phone other than the `Light` value.
For that reason, this firmware takes its wiring settings over USB only.

## Optional parts

These are not in this firmware.

- Bluefruit compatibility: a phone writes `!C` color packets to Nordic UART RX, so existing sketches work.
  Orange is `2143ffa500f7`, and the last byte is the inverted sum of the others.
- Android broadcast: the same ten bytes as service data under `f520336a-8adb-4354-a60e-1e17e4f7c56a`, non-connectable, legacy PDU.
  Firmware must never require it, because an iPhone cannot send it.

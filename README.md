# CheerLights firmware for ESP32

Firmware that makes an ESP32 board with LEDs, or a LilyGO T-Display with its screen, show the CheerLights color, over Wi-Fi or Bluetooth.

[![Release](https://img.shields.io/github/v/release/cheerlights/cheerlights-esp32)](https://github.com/cheerlights/cheerlights-esp32/releases) [![Chips](https://img.shields.io/badge/chips-ESP32%20%7C%20S3%20%7C%20C3%20%7C%20C5%20%7C%20C6%20%7C%20H2-blue)](docs/flash.md) [![CI](https://github.com/cheerlights/cheerlights-esp32/actions/workflows/ci.yml/badge.svg)](https://github.com/cheerlights/cheerlights-esp32/actions/workflows/ci.yml) [![License: MIT](https://img.shields.io/badge/license-MIT-yellow)](LICENSE)

[CheerLights](https://cheerlights.com) is a global network of lights that all show the same color, set by anyone on the [CheerLights Discord](https://cheerlights.com/discord).
This firmware makes one of those lights from an ESP32, ESP32-S3, C3, C5, C6, or H2 board.
Flash it, tell it over USB what is wired where, and choose how it gets the color.
On Wi-Fi, the light gets the color itself and needs no phone.
The CheerLights app for Android finds it on the network.
On Bluetooth, the app passes it the color, so it needs no Wi-Fi.
That suits a light you carry.
From the app, either kind can also show any color you pick, at your brightness, with an effect.

## Quick start

1. Open the [install page](https://cheerlights.github.io/cheerlights-esp32/) in Chrome or Edge on a computer, plug the board in by USB, and click Install firmware.
2. Click Connect, then pick your board from the list or let the page find your LEDs.
   The page lights each free pin until you say the LEDs are lit.
3. Choose Wi-Fi or Bluetooth.
   For Wi-Fi, pick your network from the list the board scans, or type its name, and add the password.
4. In the CheerLights app, open Devices.
   A Wi-Fi light shows up under Wi-Fi.
   A Bluetooth light shows up as `CL-` and four hex digits when you look for one.

Without a browser, write the file for your chip from the [latest release](https://github.com/cheerlights/cheerlights-esp32/releases/latest) with esptool, then set the board up in a serial monitor at 115200 baud:

```bash
pip install esptool
python -m esptool --port COM4 write-flash 0x0 cheerlights-esp32-esp32s3.bin
```

```text
board s3-matrix
link wifi
wifi "Home" "the password"
save
```

A freshly flashed board drives no pin until it is set up, because nothing tells it what is wired where.
[Flash the firmware](docs/flash.md) covers the other ways to flash, and [Set up your board](docs/configure.md) lists the presets and commands.

## Example

A LilyGO T-Display on firmware 0.5.0, set to Wi-Fi, restarts, joins its saved network, and shows green from the CheerLights feed.
The output was captured on 2026-11-10 with `restart`, then `about`.
The network name is replaced with `Home`.

```text
settings the T-Display's screen, color name hidden, not flipped, brightness cap 200, button on GPIO 35, over Wi-Fi (saved)
screen ready
ready name=CL-8F00 host=cheerlights-8f00 fw=0.5.0 chip=esp32 link=wifi saved=1
wifi scanned found=8 saved_in_range=1
wifi joining ssid="Home"
wifi connected ip=192.168.68.72 rssi=-73
api ready host=cheerlights-8f00.local port=80
cheerlights connected
frame seq=2 idx=1 rgb=008000 age=0 stale=0 off=0
about chip=esp32 fw=0.5.0 name=CL-8F00 setup=saved link=wifi saved=1 wifi=connected ip=192.168.68.72 cheerlights=connected mode=cheerlights
```

The `frame` line is green, palette color 1.
Some status lines between these are left out.
`http://cheerlights-8f00.local/` then serves the light's own page, with the color, the mode, and the brightness.

## Features

- One image per chip, written at `0x0`, for the ESP32, ESP32-S3, C3, C5, C6, and H2.
- Addressable LEDs (WS2812, SK6812, WS2811) as a strip or a grid of up to 512, a plain RGB LED on three pins, or the T-Display's 240 by 135 screen.
- An install page that flashes the right file for the chip, then sets the board up: 15 presets, from Espressif DevKits to M5Stack ATOMs, Waveshare boards, and the T-Display, or a guided search for the LED pin and color order.
- On Wi-Fi: the color over MQTT, up to eight saved networks joined strongest first, `_cheerlights._tcp` over mDNS, and a small HTTP API and page on the light ([Wi-Fi lights](docs/wifi.md)).
- On Bluetooth: a documented GATT profile, so you can build your own light or app ([BLE profile](docs/ble-profile.md)).
- Effects picked from the app or the light's page: Solid, Breathe, Sparkle, Ripple, and Rainbow on LEDs; Solid, Sparkle, Snow, and Leaves on the screen.
- Blink, from the app, flashes a light for three seconds so you can tell which one it is.
- Settings and Wi-Fi networks change over USB only, and survive flashing a newer release.
  Over the air, a phone or browser can change what the light shows and nothing else.

## Limits

- Testing so far covers two boards, the Waveshare ESP32-S3-Matrix and the LilyGO T-Display, driven by the CheerLights app on Android 17 and by a Windows PC.
  The other chips and presets have been built, not run.
- A light uses Wi-Fi or Bluetooth, not both at once.
  The ESP32-H2 has no Wi-Fi, so it is Bluetooth only.
- Neither the HTTP API nor Bluetooth writes are authenticated, so anyone on the network or nearby can change what the light shows.
  They cannot change its wiring or Wi-Fi settings.
- An iPhone app does not exist yet.
  The Bluetooth profile stays within Apple's rules, so an iPhone app can use it.

## Documentation

- [Flash the firmware](docs/flash.md): the file for each chip, the browser flasher, esptool, and updating.
- [Set up your board](docs/configure.md): presets, wiring your own LEDs, Wi-Fi, the console commands, and power.
- [Wi-Fi lights](docs/wifi.md): how a light joins, mDNS, and the local HTTP API.
- [CheerLights BLE profile](docs/ble-profile.md): the GATT service, the 10-byte color frame, `Light`, `Effects`, and `Identify`.
- [Test vectors](docs/ble-vectors.json): bytes for every Bluetooth format, for testing your own implementation.
- [Build from source](docs/building.md): the PlatformIO environments, the board test, presets, and releases.

## Development

The project builds with [PlatformIO](https://platformio.org/):

```bash
pio test -e native
pio run -e esp32s3
```

[Build from source](docs/building.md) covers every chip, the board test, adding a board preset, and releases.

## License

MIT, see [LICENSE](LICENSE).
Source Sans 3 in `fonts/` is under the SIL Open Font License, see [its license](fonts/SourceSans3-LICENSE.md).
CheerLights is a project by Hans Scharler.
Learn more and join in at [cheerlights.com](https://cheerlights.com).

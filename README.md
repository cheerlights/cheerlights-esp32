# CheerLights BLE firmware for ESP32

Firmware that makes an ESP32 board with LEDs show the CheerLights color, sent from a phone over Bluetooth.

[![Release](https://img.shields.io/github/v/release/cheerlights/cheerlights-esp32-ble)](https://github.com/cheerlights/cheerlights-esp32-ble/releases) [![Chips](https://img.shields.io/badge/chips-ESP32%20%7C%20S3%20%7C%20C3%20%7C%20C5%20%7C%20C6%20%7C%20H2-blue)](docs/flash.md) [![CI](https://github.com/cheerlights/cheerlights-esp32-ble/actions/workflows/ci.yml/badge.svg)](https://github.com/cheerlights/cheerlights-esp32-ble/actions/workflows/ci.yml) [![License: MIT](https://img.shields.io/badge/license-MIT-yellow)](LICENSE)

[CheerLights](https://cheerlights.com) is a global network of lights that all show the same color, set by anyone on the [CheerLights Discord](https://cheerlights.com/discord).
The CheerLights app for Android passes that color to a light near you over Bluetooth Low Energy, so the light needs no Wi-Fi.
This firmware is that light: flash it on an ESP32, ESP32-S3, C3, C5, C6, or H2 board, tell it over USB where its LEDs are, and connect from the app.
In the app's fun mode, the phone can also pick any color, the brightness, and an effect.

## Quick start

1. Open the [install page](https://cheerlights.github.io/cheerlights-esp32-ble/) in Chrome or Edge on a computer, plug the board in by USB, and click Install firmware.
2. Click Connect, then pick your board from the list, or let the page find your LEDs: it lights each free pin until you say they are lit.
3. In the CheerLights app, open Wearable, find the board (it shows up as `CL-` and four hex digits), and connect.

Without a browser, write the file for your chip from the [latest release](https://github.com/cheerlights/cheerlights-esp32-ble/releases/latest) with esptool, then set the board up in a serial monitor at 115200 baud:

```bash
pip install esptool
python -m esptool --port COM4 write-flash 0x0 cheerlights-esp32-ble-esp32s3.bin
```

```text
board s3-matrix
save
```

A freshly flashed board drives no pin until it is set up, because nothing tells it what is wired where.
[Flash the firmware](docs/flash.md) covers the other ways to flash, and [Set up your board](docs/configure.md) lists the presets and commands.

## Example

A Waveshare ESP32-S3-Matrix on firmware 0.3.0, set up on the install page, restarts with its saved settings, then the CheerLights app for Android connects and sends the live color (captured on 2026-11-04, with the boot ROM's lines left out):

```text
settings 64 LEDs on GPIO 14 in rows of 8, order rgb, brightness cap 16, button on GPIO 0 (saved)
ready name=CL-6B70 addr=f0:f5:bd:75:6b:71 fw=0.3.0 chip=esp32s3
light off=0
light brightness=34 effect=1 speed=75
type help for the settings commands
connected peer=62:70:ef:81:b0:be count=1
frame seq=18738 idx=1 rgb=008000 age=32 stale=0 off=0
```

The last line is green, palette color 1, set 32 minutes before the capture, shown with the Breathe effect (effect 1) the phone chose earlier.
Typing `boards` in the same console lists the presets for the chip:

```text
Presets for esp32s3:
  s3-matrix        Waveshare ESP32-S3-Matrix, 8x8
  s3-devkitc       Espressif ESP32-S3-DevKitC-1 v1.1 (untested)
  s3-devkitc-v1.0  Espressif ESP32-S3-DevKitC-1 v1.0 (untested)
  s3-zero          Waveshare ESP32-S3-Zero (untested)
  atoms3-lite      M5Stack AtomS3 Lite (untested)
```

## Features

- One image per chip, written at `0x0`, for the ESP32, ESP32-S3, C3, C5, C6, and H2.
- Addressable LEDs (WS2812, SK6812, WS2811) as a strip or a grid of up to 512, or a plain RGB LED on three pins.
- An install page that flashes the right file for the chip, then sets the board up: 14 presets, from Espressif DevKits to M5Stack ATOMs and Waveshare boards, or a guided search for the LED pin and color order.
- A fresh board drives no pin until it is set up, so it sends nothing into wiring it does not know.
- Five effects the phone picks by name (Solid, Breathe, Sparkle, Ripple, and Rainbow), with brightness and speed, kept across restarts.
- A color the phone flags as stale, because it has lost the feed, shows at a third of its brightness with the grid corners or strip ends off, and never animates.
- An optional button turns the light off and on, and the choice survives a restart.
- Settings change over USB only, and survive flashing a newer release.
  Bluetooth can change the light and nothing else.

## Limits

- Testing so far covers one board, the Waveshare ESP32-S3-Matrix, driven by the CheerLights app on Android 17 and by a Windows PC.
  The other chips and presets have been built, not run.
- The firmware shows the color it is sent and does not fetch the color over Wi-Fi.
  For a light on Wi-Fi, use the [Arduino library](https://github.com/cheerlights/cheerlights-arduino-library) or WLED.
- An iPhone app does not exist yet.
  The profile stays within Apple's Bluetooth rules, so an iPhone app can use it.
- Writes are not authenticated, so anyone nearby with a Bluetooth app can change what the light shows.
  They cannot change anything else.

## Documentation

- [Flash the firmware](docs/flash.md): the file for each chip, the browser flasher, esptool, and updating.
- [Set up your board](docs/configure.md): presets, wiring your own LEDs, the console commands, and power.
- [CheerLights BLE profile](docs/ble-profile.md): the GATT service, the 10-byte color frame, and `Light` and `Effects`, for building your own light or app.
- [Test vectors](docs/ble-vectors.json): bytes for every format, for testing your own implementation.
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
CheerLights is a project by Hans Scharler.
Learn more and join in at [cheerlights.com](https://cheerlights.com).

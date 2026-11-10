# Build from source

The firmware builds with [PlatformIO](https://platformio.org/) on the Arduino-ESP32 3.3.12 core, one environment per chip.

```bash
pip install platformio
pio test -e native              # the profile, effects, layouts, settings, console, Wi-Fi, and screen, on this computer
pio run -e esp32s3              # writes release/cheerlights-esp32-esp32s3.bin
```

| Environment | Chip | Platform |
|---|---|---|
| `esp32` | ESP32 | PlatformIO's `espressif32@8.0.0` |
| `esp32s3` | ESP32-S3 | PlatformIO's `espressif32@8.0.0` |
| `esp32c3` | ESP32-C3 | PlatformIO's `espressif32@8.0.0` |
| `esp32c6` | ESP32-C6 | PlatformIO's `espressif32@8.0.0` |
| `esp32c5` | ESP32-C5 | pioarduino `55.03.312-1` |
| `esp32h2` | ESP32-H2 | pioarduino `55.03.312-1` |

PlatformIO's own platform does not build Arduino for the C5 and H2, so those two use [pioarduino's](https://github.com/pioarduino/platform-espressif32) build of the same core.
On Windows, pioarduino's package has paths longer than 260 characters, so it installs only with long paths turned on.
To turn them on, run `New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force` in an administrator PowerShell, or build those two on Linux or in WSL.

Each build also writes the merged image for its chip to `release/`, through `tools/merge_bin.py`.

## Flash and test on a board

```bash
pio run -e esp32s3 -t upload --upload-port COM4
pip install bleak pyserial
python tools/test_ble.py --port COM4
```

`test_ble.py` acts as the phone over Bluetooth, reads the board's serial log to confirm each result, and drives the USB console.
It is written for an ESP32-S3 board that is set up and set to Bluetooth, and reads the pin and brightness cap from `show`.
It changes the colors shown while it runs, and puts back the one setting it saves.

`python tools/test_ble.py --port COM4 --show ff0000` sends one color.
`--live` sends the current CheerLights color, read once from the public feed.

## The install page

The page is plain HTML, CSS, and JavaScript in `installer/`.
It flashes with [ESP Web Tools](https://esphome.github.io/esp-web-tools/) 10.4.0, then talks to the firmware's console over Web Serial with the same commands as [Set up your board](configure.md).
It reads the presets from the board with `boards`, so the page holds no copy of them.

```bash
python tools/build_site.py
python -m http.server 8080 --directory site
```

`build_site.py` puts the page, the images from `release/`, and ESP Web Tools' `manifest.json` in `site/`.
Open `http://localhost:8080` in Chrome or Edge; Web Serial works on `localhost` without HTTPS.

Publishing a release runs the Install page workflow, which downloads that release's images, checks their checksums and partition tables, and deploys the page to GitHub Pages.
The repository's Pages source must be set to GitHub Actions.

## Where things are

- `lib/profile/`: the BLE profile's formats and the effects, with no hardware code.
- `lib/device/`: the settings, presets, pin rules, pixel layout, and console, with no hardware code.
- `lib/wifi/`: the saved networks, the `wifi` commands, the scan reply, and the local API's JSON, with no hardware code.
- `lib/screen/`: drawing, fonts, and the animations for the T-Display's screen, with no hardware code.
- `src/main.cpp`: the LEDs, the button, the effects, and the serial consoles.
- `src/ble_link.cpp` and `src/wifi_link.cpp`: the two ways a light gets its color, Bluetooth from a phone or MQTT over Wi-Fi with mDNS and the local API.
- `src/screen_out.cpp`: the T-Display's screen.
- `test/`: native tests for the four libraries.
  `tools/gen_vectors.py` turns `docs/ble-vectors.json` into the test vectors.
- `tools/preview.py`: draws the screen's scenes on this computer, with the same code as the board.
  `tools/make_fonts.py` makes `lib/screen/fonts.cpp` from Source Sans 3 in `fonts/`.
- `partitions.csv`: the app, a crash dump, then the settings store at the end of flash, so a newer image does not erase settings.
- `installer/`: the install page; `tools/build_site.py` assembles it with the images.

## Add a board

Add a preset to `PRESETS` in `lib/device/device.cpp` with the board's LED pin, count, row width, color order, brightness cap, and button.
The native tests check that every preset's pins are usable on its chip.
Mark it tested only after running it on the real board.

## Release

1. Set `FW_MAJOR`, `FW_MINOR`, and `FW_PATCH` in `src/app.h`, and `FW_TEXT` in `src/main.cpp` to match.
2. Push a tag with that version, such as `v0.5.0`.
3. The Release workflow checks the tag against the version, runs the native tests, builds every chip, and drafts a GitHub release with the images and `SHA256SUMS.txt`.
4. Read the draft and publish it.
   Publishing deploys the install page with the new images.

# Flash the firmware

Each release has one file per chip, such as `cheerlights-esp32-esp32s3.bin`.
It holds the bootloader, the partition table, and the firmware, and it is written at address `0x0`.
Pick the file for your board's chip.
The chip name is printed on the module or listed on the board's product page.

| File | Chip |
|---|---|
| `cheerlights-esp32-esp32.bin` | ESP32 (the original, such as an M5Stack ATOM or an ESP32 DevKitC) |
| `cheerlights-esp32-esp32s3.bin` | ESP32-S3 |
| `cheerlights-esp32-esp32c3.bin` | ESP32-C3 |
| `cheerlights-esp32-esp32c5.bin` | ESP32-C5 |
| `cheerlights-esp32-esp32c6.bin` | ESP32-C6 |
| `cheerlights-esp32-esp32h2.bin` | ESP32-H2 |

The ESP32-S2 and ESP8266 have no Bluetooth, so they cannot run this firmware.

`SHA256SUMS.txt` in the release lists each file's checksum.

## With the install page

The [install page](https://cheerlights.github.io/cheerlights-esp32/) picks the file for your chip, flashes it, and then sets the board up, in Chrome or Edge on a computer.
Plug the board in, click Install firmware, and choose its port.
When it asks whether to erase the device, leave Erase device unticked to keep the board's settings.
Erasing clears them, and the board lights nothing until it is set up again in step 2 of the page.

## In Espressif's web flasher

Espressif's web flasher also runs in Chrome or Edge and needs no install.

1. Open [Espressif's web flasher](https://espressif.github.io/esptool-js/).
2. Connect the board by USB, click Connect, and choose its port.
3. Under Program, set Flash Address to `0x0`, choose the `.bin` for your chip, and click Program.
4. When it finishes, press the board's reset button, or unplug it and plug it back in.

If the board does not appear or the connection fails, hold its BOOT button while plugging it in, then try again.

## With esptool

Install esptool with `pip install esptool`, then write the file at `0x0`:

```bash
python -m esptool --port COM4 write-flash 0x0 cheerlights-esp32-esp32s3.bin
```

Use your board's port in place of `COM4`, such as `/dev/ttyACM0` on Linux or `/dev/cu.usbmodem1101` on macOS.
esptool reads the chip and refuses a file built for a different one.

## After flashing

A freshly flashed board advertises as `CL-` and four hex digits, but lights nothing until it knows where its LEDs are.
[Set it up](configure.md) on the install page or in a serial monitor, then open the CheerLights app, go to Wearable, and connect.

Flashing a newer release over an older one keeps the board's settings and its last brightness and effect.
To start fresh, erase first with `python -m esptool --port COM4 erase-flash`.

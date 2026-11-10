# Set up your board

A freshly flashed board lights nothing and drives no pin until it is set up.
The firmware cannot tell what is wired to the board, and sending LED data to the wrong pin could reach whatever else is connected there.
You tell it over USB, either on the [install page](https://cheerlights.github.io/cheerlights-esp32/) or by typing commands in a serial monitor.
Settings are kept in flash, survive a restart, and survive flashing a newer release.

## With the install page

The [install page](https://cheerlights.github.io/cheerlights-esp32/) works in Chrome or Edge on a computer.
After it installs the firmware, click Connect, and it lists the presets for your board's chip.
For anything else, it lights each free pin in turn until you say your LEDs are lit, then shows two test colors to work out their color order.

The page keeps a copy of each board's setup in the browser, under the board's Bluetooth name.
If the board is erased, for example by ticking Erase device during an install, connecting it on the same browser offers to put that setup back.

## When the app says a light is not set up

A board that is not set up still connects to the CheerLights app, but it lights nothing, so the app says it needs setting up.
That happens after the first flash, after `reset`, and after an install that erased the board.
Plug it into a computer, open the install page in Chrome or Edge, and click Connect.
If this browser set the board up before, click Put its setup back; otherwise pick your board or wire-up again.

## Open the console

Connect the board with a USB data cable and open a serial monitor at 115200 baud, with DTR and RTS off so the board does not reset.

- PlatformIO: `pio device monitor -b 115200 --dtr 0 --rts 0`
- Arduino IDE: Tools, Serial Monitor, 115200 baud, line ending "Newline".
- Chrome or Edge with no install: open [Espressif's web flasher](https://espressif.github.io/esptool-js/), click Start under Console, choose the board's port, and type in the terminal there.

Type `help` and press Enter for the command list.
Chips with a built-in USB port (S3, C3, C5, C6, H2) answer on it and on UART0 at the same time, so the console works through either USB connector on boards that have two.

## Pick a preset

```text
boards
board atom-matrix
save
```

`save` stores the settings and restarts the board.
Changes wait until `save`.
`show` lists them next to the settings in use, and `cancel` drops them.

| Preset | Chip | Board | Light | Button | Tested |
|---|---|---|---|---|---|
| `s3-matrix` | ESP32-S3 | Waveshare ESP32-S3-Matrix | 64 LEDs on GPIO 14, rows of 8, RGB, cap 16 | GPIO 0 | yes |
| `s3-devkitc` | ESP32-S3 | Espressif ESP32-S3-DevKitC-1 v1.1 | 1 LED on GPIO 38 | GPIO 0 | no |
| `s3-devkitc-v1.0` | ESP32-S3 | Espressif ESP32-S3-DevKitC-1 v1.0 | 1 LED on GPIO 48 | GPIO 0 | no |
| `s3-zero` | ESP32-S3 | Waveshare ESP32-S3-Zero | 1 LED on GPIO 21, RGB | GPIO 0 | no |
| `atoms3-lite` | ESP32-S3 | M5Stack AtomS3 Lite | 1 LED on GPIO 35 | GPIO 41 | no |
| `c3-devkitm` | ESP32-C3 | Espressif ESP32-C3-DevKitM-1 | 1 LED on GPIO 8 | GPIO 9 | no |
| `c3-zero` | ESP32-C3 | Waveshare ESP32-C3-Zero | 1 LED on GPIO 10, RGB | GPIO 9 | no |
| `gledopto-c3` | ESP32-C3 | GLEDOPTO WLED ESP32 Mini, ESP32-C3 version | 50 LEDs on GPIO 10, RGB | GPIO 9 | no |
| `c5-devkitc` | ESP32-C5 | Espressif ESP32-C5-DevKitC-1 | 1 LED on GPIO 27 | GPIO 28 | no |
| `c6-devkitc` | ESP32-C6 | Espressif ESP32-C6-DevKitC-1 | 1 LED on GPIO 8 | GPIO 9 | no |
| `c6-zero` | ESP32-C6 | Waveshare ESP32-C6-Zero | 1 LED on GPIO 8, RGB | GPIO 9 | no |
| `h2-devkitm` | ESP32-H2 | Espressif ESP32-H2-DevKitM-1 | 1 LED on GPIO 8 | GPIO 9 | no |
| `atom-lite` | ESP32 | M5Stack ATOM Lite | 1 LED on GPIO 27 | GPIO 39 | no |
| `atom-matrix` | ESP32 | M5Stack ATOM Matrix | 25 LEDs on GPIO 27, rows of 5, cap 20 | GPIO 39 | no |
| `tdisplay` | ESP32 | LilyGO T-Display | Its 1.14 inch screen, backlight cap 200 | GPIO 35, next animation; GPIO 0 shows the info card | yes |

Color order is GRB unless the table says RGB, and the brightness cap is 64 unless it says otherwise.
"Tested" means run on the real board; the others come from the maker's schematic or documentation and have only been built.
The ATOM Matrix cap of 20 follows M5Stack's advice for that board's LEDs.

## Wire your own

Addressable LEDs (WS2812, SK6812, WS2811) on one data pin:

```text
pixels 5 60
order grb
max 64
save
```

Those commands set up a strip of 60 LEDs on GPIO 5.
For a grid, add the number of LEDs per row: `pixels 5 256 16` is a 16x16 matrix.
If the rows zigzag, add `serpentine on`.
If red shows as green, try another `order`.
WS2812 is usually `grb`, and WS2811 is often `rgb`.

To find the pin or the order without saving anything, light a test color:

```text
pins
test 5 60 ff0000
test off
```

`pins` lists the pins that can drive LEDs on this chip.
`test 5 60 ff0000` sends red to the first 60 LEDs on GPIO 5 and keeps it there until `test off`.
The test sends the color's bytes in the order you type them, so if `ff0000` shows green, the LEDs take green first: their order starts with `g`.
Then try `00ff00` for the second letter.

A plain RGB LED on three pins, each through a resistor:

```text
rgb 25 26 27
anode on
save
```

Use `anode on` for a common-anode LED, whose shared leg goes to 3.3 V.
Leave it off for a common-cathode LED, whose shared leg goes to ground.

A button from a GPIO to ground turns the light off and on.
Set it with `button 0`, or `button none` for no button.
The off choice survives a restart.

## Commands

| Command | What it does |
|---|---|
| `help` | The command list |
| `show` | The settings in use, and changes waiting for save |
| `export` | The settings in use as commands on one line, separated by `;`, or `none` |
| `boards` | The presets for this chip |
| `board <name>` | Start from a preset |
| `pixels <pin> <count> [width]` | Addressable LEDs: a strip, or a grid with `width` LEDs per row |
| `order <rgb\|rbg\|grb\|gbr\|brg\|bgr>` | The color order of the LEDs |
| `serpentine <on\|off>` | Grid rows that zigzag |
| `rgb <red> <green> <blue>` | A plain RGB LED on three pins |
| `anode <on\|off>` | A common-anode RGB LED |
| `max <1-255>` | The brightness cap: what 255 from the phone reaches |
| `button <pin\|none>` | A button to ground that turns the light off and on; on a screen it steps through the animations |
| `screen` | The LilyGO T-Display's built-in screen (ESP32 only) |
| `label <on\|off>` | A screen: show the color's name |
| `flip <on\|off>` | A screen: turn the picture upside down |
| `link <bluetooth\|wifi>` | Get the color from a phone over Bluetooth, or by itself over Wi-Fi ([wifi.md](wifi.md)); the ESP32-H2 is Bluetooth only |
| `save` | Keep the changes and restart |
| `cancel` | Drop changes not yet saved |
| `reset` | Forget the settings, then restart with no light set up |
| `restart` | Restart without changing anything |
| `pins` | The pins that can drive LEDs on this chip |
| `test <pin> <count> <rrggbb>` | Light a color on the first `count` LEDs of a pin, without saving |
| `test <pin> <count> <rrggbb> <rrggbb>` | Light the first color on the first half and the second on the rest, to compare two colors |
| `test off` | End the test and go back to the settings in use |
| `wifi "<name>" "<password>"` | Save a Wi-Fi network at once, newest first; quotes are needed only for spaces, and an open network takes no password |
| `wifi list` | The saved networks; the light joins the strongest one in range |
| `wifi scan` | The networks in range that the board can join, strongest first, each with its signal in dBm, and `saved` or `open` where it applies; a scan takes a few seconds, up to about 8 on a Bluetooth light |
| `wifi forget "<name>"` | Forget a network, or all of them with `wifi forget all` |
| `about` | The chip, the firmware version, the name, whether it is set up, the link, and on Wi-Fi its address and connection |

`export` prints a line such as `export pixels 14 64 8; order rgb; serpentine on; max 16; button 0`.
Typing each of those commands, then `save`, sets up another board of the same kind the same way.

The limit is 512 addressable LEDs.
The console refuses pins the chip uses for flash, PSRAM, its USB port, or the serial console, and pins that are input-only.
A typo cannot stop the board from starting.

## Power and brightness

Each WS2812 LED draws up to about 60 mA at full white, so 60 LEDs can draw 3.6 A.
Power a long strip from its own 5 V supply, connect its ground to the board's ground, and keep the brightness cap low until you know the supply can handle the current.
Some boards' LEDs run hot at full brightness.
The S3-Matrix and ATOM Matrix presets cap brightness for that reason.

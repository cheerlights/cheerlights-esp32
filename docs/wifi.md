# Wi-Fi lights

A light set to Wi-Fi gets the CheerLights color itself, over MQTT, and needs no phone.
The CheerLights app, or anything else on the same network, finds it with mDNS and changes its light over a small HTTP API.
A browser on the same network can do the same at the light's address, such as `http://cheerlights-6b70.local/`, which serves a page with the color, the mode, the palette, the animations, brightness, and, on a grid of LEDs, scrolling text.
A light set to Bluetooth works as in [ble-profile.md](ble-profile.md) instead; a light is one or the other, chosen at setup.

## Setup

Wi-Fi is set over USB only, on the install page or the console ([configure.md](configure.md)).

```
link wifi
wifi "Home" "the password"
save
```

`wifi scan` lists the networks in range, as the install page does before you pick one.
A network name in quotes is always a name, so a network called `scan` or `list` is saved with `wifi "scan" "the password"`.
A light keeps up to eight networks, newest first, and joins the strongest saved network in range.
The ESP32-H2 has no Wi-Fi, so it is Bluetooth only.

## Finding a light

| Item | Value |
|---|---|
| mDNS service | `_cheerlights._tcp`, port 80 |
| Instance name | The device name, `CL-` and four hex digits, the same as its Bluetooth name |
| Host name | `cheerlights-` and the same four hex digits, such as `cheerlights-6b70.local` |
| TXT `api` | The API version, `1` |
| TXT `fw` | The firmware version, such as `0.6.0` |
| TXT `out` | `pixels`, `rgb`, or `screen` |

## API, version 1

Plain HTTP on port 80, with JSON bodies in UTF-8.
There is no authentication, so, as with Bluetooth, everything the API changes is the light: its mode, colors, brightness, animation, and scrolling text.
Wiring and Wi-Fi are set over USB only.

Clients ignore fields they do not know, and the light ignores request fields it does not know, so either side can be newer.
`api` goes up only for a change an older client cannot read.

### `GET /api/v1/info`

```json
{
  "api": 1,
  "name": "CL-6B70",
  "fw": "0.6.0",
  "chip": "esp32s3",
  "board": "Waveshare ESP32-S3-Matrix, 8x8",
  "output": "pixels"
}
```

`board` is the preset's description, or `Custom` when the wiring was set by hand.

### `GET /api/v1/state`

```json
{
  "mode": "cheerlights",
  "cheerlights": { "name": "orange", "hex": "#ffa500", "live": true },
  "fun": { "hex": "#ff00ff" },
  "brightness": 255,
  "effect": 1,
  "speed": 128,
  "effects": [
    { "id": 0, "name": "Solid", "showsColor": true },
    { "id": 1, "name": "Breathe", "showsColor": true },
    { "id": 4, "name": "Rainbow", "showsColor": false }
  ],
  "text": { "message": "Happy holidays", "on": true, "color": "auto", "speed": 128, "turn": 0, "max": 64 }
}
```

| Field | Meaning |
|---|---|
| `mode` | `cheerlights` follows the CheerLights color; `fun` shows the fun color |
| `cheerlights` | The last CheerLights color received; `null` before the first one. `live` is false while Wi-Fi or MQTT is lost, and the light then shows the color dimmed, as a stale Bluetooth frame is |
| `fun.hex` | The color fun mode shows, kept while the light follows CheerLights |
| `brightness` | 1 to 255, a share of this light's brightness cap, as in the Bluetooth Light value |
| `effect`, `speed` | The animation and its speed, 1 to 255, slow to fast |
| `effects` | The animations this light has, which depend on its output; a screen has Solid, Sparkle, Snow, and Leaves, and the S3-Matrix adds Flow, liquid in the color that runs toward whichever side of the board is lower |
| `text` | Words that scroll over the color or the animation, from firmware 0.6.0. The field is present only on a grid of LEDs 8 by 8 or more; offer text only when it is there |
| `text.message` | Up to `max` characters: letters, numbers, spaces, and keyboard punctuation |
| `text.max` | The longest message this light takes, in characters: 64 |
| `text.on` | Whether the words scroll; `false` hides them and keeps the message |
| `text.color` | `auto`, or the letters' color as `#rrggbb`. `auto` draws white letters on a dark color and dark letters on a light color, judged by how the color looks on LEDs, and dark letters over an animation that plays its own colors. `#000000` gives dark letters: the LEDs are off where the letters are |
| `text.speed` | 1 to 255, slow to fast: 4 to 24 columns a second |
| `text.turn` | Rotation of the letters in degrees clockwise: 0, 90, 180, or 270, so the text reads upright on a grid worn or mounted in any of the four orientations |

### `POST /api/v1/state`

The body is any subset of the writable fields: `mode`, `fun`, `brightness`, `effect`, `speed`, and `text`.
`text` takes any subset of `message`, `on`, `color`, `speed`, and `turn`.

```json
{ "mode": "fun", "fun": { "hex": "#ff00ff" }, "effect": 2 }
```

```json
{ "text": { "message": "Happy holidays", "on": true } }
```

The request needs `Content-Type: application/json`; a body sent as a form answers `415`.
The light applies the whole request or none of it.
It answers `200` with the full state, or `400` with `{ "error": "..." }` and no change.

- An effect without `showsColor` is refused in CheerLights mode, and switching to CheerLights mode with one playing turns the light solid, so the light always shows the CheerLights color in that mode.
- A fun color that is a palette color shows as that color's LED value, the same as one from the phone.
- The light keeps the mode, fun color, brightness, effect, speed, and text across restarts.
- The light turns a phone keyboard's curly quotes, dashes, ellipsis, and accented letters into their plain forms, so `Don’t` is kept as `Don't`.
  Any other character, such as an emoji, is refused, as is a message longer than 64 characters after that conversion.
- New words, turning text on, or a new text speed restarts the scroll at the right edge, so the message reads from its first letter.
- A light that cannot show text refuses a `text` field, with no change.
  `"on": true` is refused when there are no words.
- A Bluetooth light takes text from the app too, from firmware 0.7.0 ([ble-profile.md](ble-profile.md#text)).

### `POST /api/v1/identify`

The light blinks white and off, a quarter second each, for 3 seconds, then shows its color again, so an owner with several lights can tell which one they are working with.
It answers `200` with `{ "identifying": 3000 }`, the blink's length in milliseconds.
Firmware before identify answers `404`.

Any other path answers `404`, and any other method `405`.

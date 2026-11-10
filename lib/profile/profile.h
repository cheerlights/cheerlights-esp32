// The CheerLights BLE profile's wire formats and the effects, from docs/ble-profile.md.
// No hardware or BLE dependencies, so the native tests cover all of it.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cheerlights {

// Values only grow: a newer phone may send more bytes than these, and the parsers read the fields they know
// and ignore the rest, so an older light keeps working with a newer phone (docs/ble-profile.md, Compatibility).
constexpr size_t FRAME_SIZE = 10;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t PALETTE_NONE = 0xFF;

struct Frame {
  uint8_t version;
  bool stale;
  uint8_t paletteIndex;
  uint8_t r, g, b;
  uint16_t sequence;
  uint16_t ageMinutes;
};

enum class ParseResult { Ok, WrongLength, UnknownVersion };

// A receiver ignores any result other than Ok. A frame shorter than FRAME_SIZE is WrongLength; bytes past it
// and flag bits it does not know are ignored, so a later revision can add either without breaking this firmware.
ParseResult parseFrame(const uint8_t* data, size_t length, Frame& out);

// The bytes of a frame, so a read returns what was last accepted.
void encodeFrame(const Frame& frame, uint8_t out[FRAME_SIZE]);

// How a frame looks. A stale color is dimmed to a third, and its marker pixels are off.
struct Shown {
  uint8_t r, g, b;
  bool markersOff;
};
Shown renderFrame(const Frame& frame);

// The Info characteristic: protocol version, firmware version, and what this light has, then from firmware 0.5.0
// the board's name, as a length byte and UTF-8 text.
constexpr size_t INFO_SIZE = 5;  // the fields every firmware writes
constexpr size_t INFO_BOARD_MAX = 40;
constexpr size_t INFO_MAX_SIZE = INFO_SIZE + 1 + INFO_BOARD_MAX;
constexpr uint8_t CAP_DISPLAY = 0x01;      // it shows the color
constexpr uint8_t CAP_OFF_CONTROL = 0x02;  // it has a physical off control
constexpr uint8_t CAP_LIGHT = 0x04;        // it has Light and Effects
constexpr uint8_t CAP_NEEDS_SETUP = 0x08;  // it is not set up, so it shows nothing until it is set up over USB
constexpr uint8_t CAP_IDENTIFY = 0x10;     // it has Identify, so a phone can make it blink

struct Info {
  uint8_t major, minor, patch;
  bool display, offControl, light, needsSetup;
  bool identify = false;
  const char* board = nullptr;  // null before firmware 0.5.0, which sent five bytes
};
// Returns the length written: five bytes, then the board's name when there is one, cut to INFO_BOARD_MAX bytes at
// a whole character.
size_t encodeInfo(const Info& info, uint8_t out[INFO_MAX_SIZE]);

// The Identify characteristic: a phone writes one byte, the version, and the light blinks so its owner can tell
// which light it is. Like every characteristic, it changes the light only.
constexpr uint8_t IDENTIFY_VERSION = 1;
ParseResult parseIdentify(const uint8_t* data, size_t length);

// The Light characteristic: brightness, effect, and speed, written by the phone and kept across restarts.
constexpr size_t LIGHT_SIZE = 4;
constexpr uint8_t LIGHT_VERSION = 1;

struct Light {
  uint8_t brightness = 255;  // a share of this device's brightness cap, 1 to 255
  uint8_t effect = 0;        // an id from EFFECTS; unknown ids show solid
  uint8_t speed = 128;       // 1 to 255, slow to fast
};

// Zero brightness or speed reads as 1, as the profile says.
ParseResult parseLight(const uint8_t* data, size_t length, Light& out);
void encodeLight(const Light& light, uint8_t out[LIGHT_SIZE]);

// The animations this firmware has. Entry 0 is always solid.
enum EffectId : uint8_t {
  EFFECT_SOLID = 0,
  EFFECT_BREATHE = 1,
  EFFECT_SPARKLE = 2,
  EFFECT_RIPPLE = 3,
  EFFECT_RAINBOW = 4,
  EFFECT_SNOW = 5,    // a screen only
  EFFECT_LEAVES = 6,  // a screen only
};

constexpr uint8_t EFFECTS_VERSION = 3;

// Effect flag bits. A phone following the CheerLights color offers only effects that show it.
constexpr uint8_t EFFECT_SHOWS_COLOR = 0x01;  // animates the color it is sent, instead of drawing its own colors

struct EffectInfo {
  uint8_t id;
  uint8_t flags;
  const char* name;
};
// The LED animations.
extern const EffectInfo EFFECTS[];
extern const size_t EFFECT_COUNT;
// A screen's animations, drawn over the color.
extern const EffectInfo SCREEN_EFFECTS[];
extern const size_t SCREEN_EFFECT_COUNT;
// The flags of an effect in a list, or EFFECT_SHOWS_COLOR for an id not in it, which shows solid.
uint8_t effectFlags(const EffectInfo* effects, size_t count, uint8_t id);
bool hasEffect(const EffectInfo* effects, size_t count, uint8_t id);

// Writes the Effects characteristic value and returns its length, or 0 if it does not fit in capacity.
// Each entry starts with its own size, so a later firmware can add a field to an entry and older phones skip it.
size_t encodeEffects(const EffectInfo* effects, size_t count, uint8_t* out, size_t capacity);

struct Rgb8 {
  uint8_t r, g, b;
};
inline bool operator==(Rgb8 a, Rgb8 b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
inline bool operator!=(Rgb8 a, Rgb8 b) { return !(a == b); }

// The color of one pixel of an effect at a moment, at column x and row y of a width by height layout.
// A strip is one row, and a single LED is 1 by 1. Pure: the same inputs always give the same pixel.
Rgb8 effectPixel(uint8_t effect, Rgb8 color, uint8_t speed, uint32_t ms, int x, int y, int width, int height);

// True when the effect changes over time and needs redrawing.
bool effectMoves(uint8_t effect);

// What an LED is sent for a color, so it looks the way the color looks on a screen. An LED's light grows in a
// straight line with the value it is sent, but the eye does not, so without this pink (ffc0cb) looks almost white.
// Gamma 2.8, as WLED uses; screens correct for themselves and do not need it.
uint8_t ledLevel(uint8_t value);
Rgb8 ledColor(Rgb8 color);

// What an LED shows for each palette color, by palette index (docs/colors.json). Gamma alone left pink pale and
// green dim, so each was picked by eye on a Waveshare ESP32-S3-Matrix (2026-11-09). Purple is a dark color an LED
// cannot show, so its value is the one that stays apart from blue and magenta.
constexpr size_t PALETTE_SIZE = 11;
extern const Rgb8 LED_PALETTE[PALETTE_SIZE];

// The CheerLights palette as published (docs/colors.json), in palette index order: what a screen shows.
struct PaletteColor {
  const char* name;
  Rgb8 rgb;
};
extern const PaletteColor PALETTE[PALETTE_SIZE];
// The palette index of an exact palette color, or PALETTE_NONE.
uint8_t paletteIndexOf(Rgb8 color);

// The LED color for a frame: the palette's LED value when the frame names a palette color, and its color through
// gamma otherwise, such as a color from the phone's color wheel.
Rgb8 ledBase(const Frame& frame);

}  // namespace cheerlights

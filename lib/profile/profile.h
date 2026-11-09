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
enum EffectId : uint8_t { EFFECT_SOLID = 0, EFFECT_BREATHE = 1, EFFECT_SPARKLE = 2, EFFECT_RIPPLE = 3, EFFECT_RAINBOW = 4 };

constexpr uint8_t EFFECTS_VERSION = 3;

// Effect flag bits. A phone following the CheerLights color offers only effects that show it.
constexpr uint8_t EFFECT_SHOWS_COLOR = 0x01;  // animates the color it is sent, instead of drawing its own colors

struct EffectInfo {
  uint8_t id;
  uint8_t flags;
  const char* name;
};
extern const EffectInfo EFFECTS[];
extern const size_t EFFECT_COUNT;

// Writes the Effects characteristic value and returns its length, or 0 if it does not fit in capacity.
// Each entry starts with its own size, so a later firmware can add a field to an entry and older phones skip it.
size_t encodeEffects(const EffectInfo* effects, size_t count, uint8_t* out, size_t capacity);

struct Rgb8 {
  uint8_t r, g, b;
};

// The color of one pixel of an effect at a moment, at column x and row y of a width by height layout.
// A strip is one row, and a single LED is 1 by 1. Pure: the same inputs always give the same pixel.
Rgb8 effectPixel(uint8_t effect, Rgb8 color, uint8_t speed, uint32_t ms, int x, int y, int width, int height);

// True when the effect changes over time and needs redrawing.
bool effectMoves(uint8_t effect);

}  // namespace cheerlights

#include "profile.h"

#include <math.h>
#include <string.h>

namespace cheerlights {

ParseResult parseFrame(const uint8_t* data, size_t length, Frame& out) {
  if (data == nullptr || length < FRAME_SIZE) return ParseResult::WrongLength;
  const uint8_t version = data[0] >> 4;
  if (version != PROTOCOL_VERSION) return ParseResult::UnknownVersion;
  out.version = version;
  out.stale = (data[0] & 0x01) != 0;
  out.paletteIndex = data[1];
  out.r = data[2];
  out.g = data[3];
  out.b = data[4];
  out.sequence = static_cast<uint16_t>(data[5] | (data[6] << 8));
  out.ageMinutes = static_cast<uint16_t>(data[7] | (data[8] << 8));
  return ParseResult::Ok;
}

void encodeFrame(const Frame& frame, uint8_t out[FRAME_SIZE]) {
  out[0] = static_cast<uint8_t>((PROTOCOL_VERSION << 4) | (frame.stale ? 1 : 0));
  out[1] = frame.paletteIndex;
  out[2] = frame.r;
  out[3] = frame.g;
  out[4] = frame.b;
  out[5] = static_cast<uint8_t>(frame.sequence);
  out[6] = static_cast<uint8_t>(frame.sequence >> 8);
  out[7] = static_cast<uint8_t>(frame.ageMinutes);
  out[8] = static_cast<uint8_t>(frame.ageMinutes >> 8);
  out[9] = 0;
}

Shown renderFrame(const Frame& frame) {
  if (!frame.stale) return {frame.r, frame.g, frame.b, false};
  return {static_cast<uint8_t>(frame.r / 3), static_cast<uint8_t>(frame.g / 3),
          static_cast<uint8_t>(frame.b / 3), true};
}

ParseResult parseLight(const uint8_t* data, size_t length, Light& out) {
  if (data == nullptr || length < LIGHT_SIZE) return ParseResult::WrongLength;
  if (data[0] != LIGHT_VERSION) return ParseResult::UnknownVersion;
  out.brightness = data[1] == 0 ? 1 : data[1];
  out.effect = data[2];
  out.speed = data[3] == 0 ? 1 : data[3];
  return ParseResult::Ok;
}

void encodeLight(const Light& light, uint8_t out[LIGHT_SIZE]) {
  out[0] = LIGHT_VERSION;
  out[1] = light.brightness == 0 ? 1 : light.brightness;
  out[2] = light.effect;
  out[3] = light.speed == 0 ? 1 : light.speed;
}

void encodeInfo(const Info& info, uint8_t out[INFO_SIZE]) {
  out[0] = PROTOCOL_VERSION;
  out[1] = info.major;
  out[2] = info.minor;
  out[3] = info.patch;
  out[4] = (info.display ? CAP_DISPLAY : 0) | (info.offControl ? CAP_OFF_CONTROL : 0) | (info.light ? CAP_LIGHT : 0) |
           (info.needsSetup ? CAP_NEEDS_SETUP : 0);
}

const EffectInfo EFFECTS[] = {
    {EFFECT_SOLID, EFFECT_SHOWS_COLOR, "Solid"},     {EFFECT_BREATHE, EFFECT_SHOWS_COLOR, "Breathe"},
    {EFFECT_SPARKLE, EFFECT_SHOWS_COLOR, "Sparkle"}, {EFFECT_RIPPLE, EFFECT_SHOWS_COLOR, "Ripple"},
    {EFFECT_RAINBOW, 0, "Rainbow"},
};
const size_t EFFECT_COUNT = sizeof(EFFECTS) / sizeof(EFFECTS[0]);

size_t encodeEffects(const EffectInfo* effects, size_t count, uint8_t* out, size_t capacity) {
  if (count > 255 || capacity < 2) return 0;
  size_t at = 0;
  out[at++] = EFFECTS_VERSION;
  out[at++] = static_cast<uint8_t>(count);
  for (size_t i = 0; i < count; i++) {
    const size_t length = strlen(effects[i].name);
    if (length > 20 || at + 4 + length > capacity) return 0;
    out[at++] = static_cast<uint8_t>(3 + length);  // the bytes after this one: id, flags, name length, name
    out[at++] = effects[i].id;
    out[at++] = effects[i].flags;
    out[at++] = static_cast<uint8_t>(length);
    memcpy(out + at, effects[i].name, length);
    at += length;
  }
  return at;
}

namespace {

constexpr float TWO_PI_F = 6.2831853f;

// Speed 1 is a slow 6 s cycle and 255 a quick 0.6 s one.
float periodMs(uint8_t speed) {
  const uint8_t s = speed == 0 ? 1 : speed;
  return 6000.0f - (s - 1) * (5400.0f / 254.0f);
}

float phase(uint32_t ms, uint8_t speed) {
  const float p = periodMs(speed);
  return fmodf(static_cast<float>(ms), p) / p;
}

Rgb8 scale(Rgb8 c, float level) {
  if (level < 0) level = 0;
  if (level > 1) level = 1;
  return {static_cast<uint8_t>(c.r * level + 0.5f), static_cast<uint8_t>(c.g * level + 0.5f),
          static_cast<uint8_t>(c.b * level + 0.5f)};
}

// A fixed scatter of per-pixel offsets, so sparkles look random but repeat exactly.
float scatter(int x, int y) {
  uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return (h & 0xFFFF) / 65536.0f;
}

Rgb8 hue(float degrees) {
  const float h = fmodf(fmodf(degrees, 360.0f) + 360.0f, 360.0f) / 60.0f;
  const float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
  float r = 0, g = 0, b = 0;
  switch (static_cast<int>(h)) {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
  }
  return {static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255)};
}

}  // namespace

bool effectMoves(uint8_t effect) {
  return effect == EFFECT_BREATHE || effect == EFFECT_SPARKLE || effect == EFFECT_RIPPLE || effect == EFFECT_RAINBOW;
}

Rgb8 effectPixel(uint8_t effect, Rgb8 color, uint8_t speed, uint32_t ms, int x, int y, int width, int height) {
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  const float t = phase(ms, speed);
  switch (effect) {
    case EFFECT_BREATHE:
      // Never fully dark, so the wearer can still see the color at the bottom of a breath.
      return scale(color, 0.15f + 0.85f * (0.5f - 0.5f * cosf(TWO_PI_F * t)));
    case EFFECT_SPARKLE: {
      const float p = fmodf(t + scatter(x, y), 1.0f);
      return scale(color, p < 0.12f ? 1.0f - p / 0.12f * 0.75f : 0.25f);
    }
    case EFFECT_RIPPLE: {
      // Rings move out from the center, four pixels apart; a single LED pulses.
      const float cx = (width - 1) / 2.0f, cy = (height - 1) / 2.0f;
      const float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
      return scale(color, 0.1f + 0.9f * (0.5f + 0.5f * cosf(TWO_PI_F * (d / 4.0f - t))));
    }
    case EFFECT_RAINBOW:
      // One rainbow across the diagonal, so any size shows the whole wheel; a single LED cycles through it.
      return hue((x + y) * (360.0f / (width + height)) + t * 360.0f);
    default:
      return color;
  }
}

}  // namespace cheerlights

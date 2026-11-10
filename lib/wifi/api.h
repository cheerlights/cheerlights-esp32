// The JSON of a Wi-Fi light's local API (docs/wifi.md), apart from the network, so a computer tests all of it.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "profile.h"

namespace cheerlights {

// What the owner chose: the mode, the fun color, and the light (brightness, effect, speed).
struct LightState {
  bool fun = false;  // false follows CheerLights
  Rgb8 funColor{0xff, 0x00, 0xff};
  Light light;
};

// The last CheerLights color, and whether the light is still hearing from CheerLights.
struct CheerLightsState {
  bool have = false;
  Rgb8 rgb{0, 0, 0};
  bool live = false;
};

struct DeviceInfo {
  const char* name;
  const char* fw;
  const char* chip;
  const char* board;
  const char* output;  // "pixels", "rgb", or "screen"
};

constexpr int API_VERSION = 1;

// Each writes the JSON text and returns its length, or 0 if it does not fit.
size_t writeInfo(const DeviceInfo& info, char* out, size_t size);
size_t writeState(const LightState& state, const CheerLightsState& cheerlights, const EffectInfo* effects,
                  size_t effectCount, char* out, size_t size);

// Applies a POST body to state, all or nothing: nullptr when it applied, or why it did not, for the 400 reply.
const char* applyState(const char* body, size_t length, const EffectInfo* effects, size_t effectCount,
                       LightState& state);

// The frame the light shows: the CheerLights color in CheerLights mode, dimmed as stale while it is not live,
// or the fun color. haveColor is false before the first CheerLights color.
Frame frameFor(const LightState& state, const CheerLightsState& cheerlights, bool& haveColor);

// Reads a CheerLights MQTT payload, "#rrggbb" as the topic cheerlightsRGB sends it, ignoring spaces around it.
bool parseColorPayload(const char* text, size_t length, Rgb8& out);

// The mode and fun color, kept in flash beside the Light value.
constexpr size_t MODE_SIZE = 4;
void encodeMode(const LightState& state, uint8_t out[MODE_SIZE]);
bool decodeMode(const uint8_t* data, size_t length, LightState& state);

}  // namespace cheerlights

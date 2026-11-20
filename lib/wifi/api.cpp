#include "api.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace cheerlights {

namespace {

void hexText(Rgb8 c, char out[8]) { snprintf(out, 8, "#%02x%02x%02x", c.r, c.g, c.b); }

// "#rrggbb", either case.
bool parseHexText(const char* text, Rgb8& out) {
  if (text == nullptr || strlen(text) != 7 || text[0] != '#') return false;
  uint8_t v[3];
  for (int i = 0; i < 3; i++) {
    int byte = 0;
    for (int k = 0; k < 2; k++) {
      const char c = static_cast<char>(tolower(static_cast<unsigned char>(text[1 + i * 2 + k])));
      int d;
      if (c >= '0' && c <= '9') {
        d = c - '0';
      } else if (c >= 'a' && c <= 'f') {
        d = c - 'a' + 10;
      } else {
        return false;
      }
      byte = byte * 16 + d;
    }
    v[i] = static_cast<uint8_t>(byte);
  }
  out = {v[0], v[1], v[2]};
  return true;
}

// A whole number from 1 to 255, as brightness and speed take.
bool level(JsonVariantConst v, uint8_t& out) {
  if (!v.is<int>()) return false;
  const int n = v.as<int>();
  if (n < 1 || n > 255) return false;
  out = static_cast<uint8_t>(n);
  return true;
}

}  // namespace

size_t writeInfo(const DeviceInfo& info, char* out, size_t size) {
  JsonDocument doc;
  doc["api"] = API_VERSION;
  doc["name"] = info.name;
  doc["fw"] = info.fw;
  doc["chip"] = info.chip;
  doc["board"] = info.board;
  doc["output"] = info.output;
  if (measureJson(doc) + 1 > size) return 0;
  return serializeJson(doc, out, size);
}

size_t writeState(const LightState& state, const CheerLightsState& cheerlights, const EffectInfo* effects,
                  size_t effectCount, bool scrolls, char* out, size_t size) {
  JsonDocument doc;
  doc["mode"] = state.fun ? "fun" : "cheerlights";
  char hex[8];
  if (cheerlights.have) {
    JsonObject cl = doc["cheerlights"].to<JsonObject>();
    const uint8_t index = paletteIndexOf(cheerlights.rgb);
    if (index == PALETTE_NONE) {
      cl["name"] = nullptr;
    } else {
      cl["name"] = PALETTE[index].name;
    }
    hexText(cheerlights.rgb, hex);
    cl["hex"] = hex;
    cl["live"] = cheerlights.live;
  } else {
    doc["cheerlights"] = nullptr;
  }
  hexText(state.funColor, hex);
  doc["fun"]["hex"] = hex;
  doc["brightness"] = state.light.brightness;
  doc["effect"] = state.light.effect;
  doc["speed"] = state.light.speed;
  JsonArray list = doc["effects"].to<JsonArray>();
  for (size_t i = 0; i < effectCount; i++) {
    JsonObject e = list.add<JsonObject>();
    e["id"] = effects[i].id;
    e["name"] = effects[i].name;
    e["showsColor"] = (effects[i].flags & EFFECT_SHOWS_COLOR) != 0;
  }
  if (scrolls) {
    JsonObject t = doc["text"].to<JsonObject>();
    t["message"] = state.text.message;
    t["on"] = state.text.on;
    if (state.text.autoColor) {
      t["color"] = "auto";
    } else {
      hexText(state.text.color, hex);
      t["color"] = hex;
    }
    t["speed"] = state.text.speed;
    t["turn"] = state.text.turn * 90;
    t["max"] = TEXT_MAX;
  }
  if (measureJson(doc) + 1 > size) return 0;
  return serializeJson(doc, out, size);
}

const char* applyState(const char* body, size_t length, const EffectInfo* effects, size_t effectCount, bool scrolls,
                       LightState& state) {
  JsonDocument doc;
  if (deserializeJson(doc, body, length) != DeserializationError::Ok) return "The body is not JSON.";
  if (!doc.is<JsonObject>()) return "The body is a JSON object.";
  LightState next = state;

  JsonVariantConst mode = doc["mode"];
  if (!mode.isNull()) {
    const char* m = mode.is<const char*>() ? mode.as<const char*>() : "";
    if (strcmp(m, "fun") == 0) {
      next.fun = true;
    } else if (strcmp(m, "cheerlights") == 0) {
      next.fun = false;
    } else {
      return "mode is cheerlights or fun.";
    }
  }
  JsonVariantConst fun = doc["fun"];
  if (!fun.isNull()) {
    JsonVariantConst hex = fun["hex"];
    if (!fun.is<JsonObjectConst>() || !hex.is<const char*>() || !parseHexText(hex.as<const char*>(), next.funColor)) {
      return "fun is an object with hex, such as {\"hex\": \"#ff00ff\"}.";
    }
  }
  JsonVariantConst brightness = doc["brightness"];
  if (!brightness.isNull() && !level(brightness, next.light.brightness)) return "brightness is a whole number, 1 to 255.";
  JsonVariantConst speed = doc["speed"];
  if (!speed.isNull() && !level(speed, next.light.speed)) return "speed is a whole number, 1 to 255.";
  JsonVariantConst effect = doc["effect"];
  if (!effect.isNull()) {
    if (!effect.is<int>() || effect.as<int>() < 0 || effect.as<int>() > 255 ||
        !hasEffect(effects, effectCount, static_cast<uint8_t>(effect.as<int>()))) {
      return "effect is an id from this light's effects.";
    }
    next.light.effect = static_cast<uint8_t>(effect.as<int>());
  }
  // In CheerLights mode the light always shows the CheerLights color.
  if (!next.fun && (effectFlags(effects, effectCount, next.light.effect) & EFFECT_SHOWS_COLOR) == 0) {
    if (!effect.isNull()) return "In CheerLights mode the light takes only effects that show the color.";
    next.light.effect = EFFECT_SOLID;
  }
  JsonVariantConst text = doc["text"];
  if (!text.isNull()) {
    if (!scrolls) return "This light cannot show text. It needs a grid of at least 8 by 8 LEDs.";
    if (!text.is<JsonObjectConst>()) return "text is an object, such as {\"message\": \"Hello\", \"on\": true}.";
    JsonVariantConst message = text["message"];
    if (!message.isNull()) {
      if (!message.is<const char*>()) return "text.message is a string.";
      const char* why = normalizeText(message.as<const char*>(), next.text.message);
      if (why != nullptr) return why;
    }
    JsonVariantConst on = text["on"];
    if (!on.isNull()) {
      if (!on.is<bool>()) return "text.on is true or false.";
      next.text.on = on.as<bool>();
    }
    JsonVariantConst color = text["color"];
    if (!color.isNull()) {
      const char* c = color.is<const char*>() ? color.as<const char*>() : "";
      if (strcmp(c, "auto") == 0) {
        next.text.autoColor = true;
      } else if (parseHexText(c, next.text.color)) {
        next.text.autoColor = false;
      } else {
        return "text.color is auto or a color such as #ffffff.";
      }
    }
    JsonVariantConst textSpeed = text["speed"];
    if (!textSpeed.isNull() && !level(textSpeed, next.text.speed)) return "text.speed is a whole number, 1 to 255.";
    JsonVariantConst turn = text["turn"];
    if (!turn.isNull()) {
      const int degrees = turn.is<int>() ? turn.as<int>() : -1;
      if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
        return "text.turn is 0, 90, 180, or 270 degrees, clockwise.";
      }
      next.text.turn = static_cast<uint8_t>(degrees / 90);
    }
    if (next.text.on && next.text.message[0] == '\0') return "There are no words yet. Add some, then turn the text on.";
  }
  state = next;
  return nullptr;
}

Frame frameFor(const LightState& state, const CheerLightsState& cheerlights, bool& haveColor) {
  Frame f{};
  f.version = PROTOCOL_VERSION;
  if (state.fun) {
    haveColor = true;
    f.r = state.funColor.r;
    f.g = state.funColor.g;
    f.b = state.funColor.b;
    f.paletteIndex = paletteIndexOf(state.funColor);
    return f;
  }
  haveColor = cheerlights.have;
  f.r = cheerlights.rgb.r;
  f.g = cheerlights.rgb.g;
  f.b = cheerlights.rgb.b;
  f.paletteIndex = paletteIndexOf(cheerlights.rgb);
  f.stale = cheerlights.have && !cheerlights.live;
  return f;
}

bool parseColorPayload(const char* text, size_t length, Rgb8& out) {
  size_t start = 0, end = length;
  while (start < end && isspace(static_cast<unsigned char>(text[start]))) start++;
  while (end > start && isspace(static_cast<unsigned char>(text[end - 1]))) end--;
  if (end - start != 7) return false;
  char hex[8];
  memcpy(hex, text + start, 7);
  hex[7] = '\0';
  return parseHexText(hex, out);
}

void encodeMode(const LightState& state, uint8_t out[MODE_SIZE]) {
  out[0] = state.fun ? 1 : 0;
  out[1] = state.funColor.r;
  out[2] = state.funColor.g;
  out[3] = state.funColor.b;
}

bool decodeMode(const uint8_t* data, size_t length, LightState& state) {
  if (data == nullptr || length != MODE_SIZE || data[0] > 1) return false;
  state.fun = data[0] == 1;
  state.funColor = {data[1], data[2], data[3]};
  return true;
}

}  // namespace cheerlights

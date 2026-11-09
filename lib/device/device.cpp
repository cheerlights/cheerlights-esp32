#include "device.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cheerlights {

bool operator==(const Settings& a, const Settings& b) {
  return a.output == b.output && a.pixelPin == b.pixelPin && a.count == b.count && a.width == b.width &&
         a.serpentine == b.serpentine && a.order == b.order && a.redPin == b.redPin && a.greenPin == b.greenPin &&
         a.bluePin == b.bluePin && a.commonAnode == b.commonAnode && a.maxBrightness == b.maxBrightness &&
         a.buttonPin == b.buttonPin;
}

namespace {

Settings pixels(int pin, int count, int width, ColorOrder order, int cap, int button) {
  Settings s;
  s.output = Output::Pixels;
  s.pixelPin = static_cast<int8_t>(pin);
  s.count = static_cast<uint16_t>(count);
  s.width = static_cast<uint16_t>(width);
  s.order = order;
  s.maxBrightness = static_cast<uint8_t>(cap);
  s.buttonPin = static_cast<int8_t>(button);
  return s;
}

}  // namespace

// Boards with known wiring, grouped by chip. `boards` lists a chip's entries in this order.
const Preset PRESETS[] = {
    {"s3-matrix", "esp32s3", "Waveshare ESP32-S3-Matrix, 8x8", pixels(14, 64, 8, ColorOrder::RGB, 16, 0), true},
    {"s3-devkitc", "esp32s3", "Espressif ESP32-S3-DevKitC-1 v1.1", pixels(38, 1, 0, ColorOrder::GRB, 64, 0), false},
    {"s3-devkitc-v1.0", "esp32s3", "Espressif ESP32-S3-DevKitC-1 v1.0", pixels(48, 1, 0, ColorOrder::GRB, 64, 0), false},
    {"s3-zero", "esp32s3", "Waveshare ESP32-S3-Zero", pixels(21, 1, 0, ColorOrder::RGB, 64, 0), false},
    {"atoms3-lite", "esp32s3", "M5Stack AtomS3 Lite", pixels(35, 1, 0, ColorOrder::GRB, 64, 41), false},
    {"c3-devkitm", "esp32c3", "Espressif ESP32-C3-DevKitM-1", pixels(8, 1, 0, ColorOrder::GRB, 64, 9), false},
    {"c3-zero", "esp32c3", "Waveshare ESP32-C3-Zero", pixels(10, 1, 0, ColorOrder::RGB, 64, 9), false},
    {"gledopto-c3", "esp32c3", "GLEDOPTO WLED ESP32 Mini, a strip of 50", pixels(10, 50, 0, ColorOrder::RGB, 64, 9),
     false},
    {"c6-devkitc", "esp32c6", "Espressif ESP32-C6-DevKitC-1", pixels(8, 1, 0, ColorOrder::GRB, 64, 9), false},
    {"c6-zero", "esp32c6", "Waveshare ESP32-C6-Zero", pixels(8, 1, 0, ColorOrder::RGB, 64, 9), false},
    {"h2-devkitm", "esp32h2", "Espressif ESP32-H2-DevKitM-1", pixels(8, 1, 0, ColorOrder::GRB, 64, 9), false},
    {"c5-devkitc", "esp32c5", "Espressif ESP32-C5-DevKitC-1", pixels(27, 1, 0, ColorOrder::GRB, 64, 28), false},
    {"atom-lite", "esp32", "M5Stack ATOM Lite", pixels(27, 1, 0, ColorOrder::GRB, 64, 39), false},
    {"atom-matrix", "esp32", "M5Stack ATOM Matrix, 5x5", pixels(27, 25, 5, ColorOrder::GRB, 20, 39), false},
};
const size_t PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

const Preset* findPreset(const char* name) {
  for (size_t i = 0; i < PRESET_COUNT; i++) {
    if (strcmp(PRESETS[i].name, name) == 0) return &PRESETS[i];
  }
  return nullptr;
}

bool isSetUp(const Settings& s) { return s.output != Output::None; }

namespace {

struct ChipPins {
  const char* chip;
  int maxGpio;
  const int* blocked;  // flash, PSRAM, USB, the serial console, and numbers with no pin
  size_t blockedCount;
  int firstInputOnly;  // pins from here up read but cannot drive; -1 when none
};

constexpr int ESP32_BLOCKED[] = {1, 3, 6, 7, 8, 9, 10, 11, 20, 24, 28, 29, 30, 31};
constexpr int ESP32S3_BLOCKED[] = {19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 43, 44};
constexpr int ESP32C3_BLOCKED[] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
constexpr int ESP32C6_BLOCKED[] = {12, 13, 16, 17, 24, 25, 26, 27, 28, 29, 30};
constexpr int ESP32H2_BLOCKED[] = {15, 16, 17, 18, 19, 20, 21, 23, 24, 26, 27};
constexpr int ESP32C5_BLOCKED[] = {11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22};

#define CHIP(name, max, list, inputOnly) {name, max, list, sizeof(list) / sizeof(list[0]), inputOnly}
const ChipPins CHIPS[] = {
    CHIP("esp32", 39, ESP32_BLOCKED, 34),     CHIP("esp32s3", 48, ESP32S3_BLOCKED, -1),
    CHIP("esp32c3", 21, ESP32C3_BLOCKED, -1), CHIP("esp32c6", 30, ESP32C6_BLOCKED, -1),
    CHIP("esp32h2", 27, ESP32H2_BLOCKED, -1), CHIP("esp32c5", 28, ESP32C5_BLOCKED, -1),
};
#undef CHIP

}  // namespace

bool pinUsable(const char* chip, int pin, bool output) {
  for (const ChipPins& c : CHIPS) {
    if (strcmp(c.chip, chip) != 0) continue;
    if (pin < 0 || pin > c.maxGpio) return false;
    for (size_t i = 0; i < c.blockedCount; i++) {
      if (c.blocked[i] == pin) return false;
    }
    return !(output && c.firstInputOnly >= 0 && pin >= c.firstInputOnly);
  }
  return false;
}

const char* problem(const char* chip, const Settings& s) {
  if (s.buttonPin >= 0 && !pinUsable(chip, s.buttonPin, false)) return "the button pin is not usable on this chip";
  if (s.output == Output::Pixels) {
    if (!pinUsable(chip, s.pixelPin, true)) return "the LED pin is not usable on this chip";
    if (s.count < 1 || s.count > MAX_PIXELS) return "the LED count must be 1 to 512";
    if (s.width > s.count) return "the row width cannot be more than the LED count";
    if (s.buttonPin == s.pixelPin) return "the button and the LEDs need different pins";
  } else if (s.output == Output::Rgb) {
    const int pins[3] = {s.redPin, s.greenPin, s.bluePin};
    for (int p : pins) {
      if (!pinUsable(chip, p, true)) return "an RGB LED pin is not usable on this chip";
      if (p == s.buttonPin) return "the button and the LED need different pins";
    }
    if (pins[0] == pins[1] || pins[1] == pins[2] || pins[0] == pins[2]) return "the RGB LED needs three different pins";
  }
  if (s.maxBrightness < 1) return "the brightness cap must be 1 to 255";
  return nullptr;
}

int pixelCount(const Settings& s) {
  switch (s.output) {
    case Output::Pixels:
      return s.count;
    case Output::Rgb:
      return 1;
    default:
      return 0;
  }
}

Layout layoutOf(const Settings& s) {
  const int count = pixelCount(s);
  if (count == 0) return {0, 0};
  if (s.output != Output::Pixels || s.width == 0 || s.width >= count) return {count, 1};
  return {s.width, (count + s.width - 1) / s.width};
}

void pixelXY(const Settings& s, int index, int& x, int& y) {
  const Layout l = layoutOf(s);
  if (l.width == 0) {
    x = y = 0;
    return;
  }
  y = index / l.width;
  x = index % l.width;
  if (s.serpentine && (y % 2) == 1) x = l.width - 1 - x;
}

bool isMarker(const Settings& s, int index) {
  const Layout l = layoutOf(s);
  int x, y;
  pixelXY(s, index, x, y);
  if (l.height > 1) return (x == 0 || x == l.width - 1) && (y == 0 || y == l.height - 1);
  return l.width >= 3 && (x == 0 || x == l.width - 1);
}

int alivePixel(const Settings& s) {
  const Layout l = layoutOf(s);
  if (l.width == 0) return -1;
  const int middle = (l.height / 2) * l.width + l.width / 2;
  const int count = pixelCount(s);
  for (int i = 0; i < count; i++) {
    int x, y;
    pixelXY(s, i, x, y);
    if (y * l.width + x == middle) return i;
  }
  return 0;
}

Rgb8 scenePixel(const Settings& s, const Scene& scene, int index) {
  if (scene.off) return {0, 0, 0};
  if (!scene.haveColor) return index == alivePixel(s) ? Rgb8{40, 40, 40} : Rgb8{0, 0, 0};
  if (scene.frame.stale) {
    const Shown shown = renderFrame(scene.frame);
    if (shown.markersOff && isMarker(s, index)) return {0, 0, 0};
    return {shown.r, shown.g, shown.b};
  }
  const Layout l = layoutOf(s);
  int x, y;
  pixelXY(s, index, x, y);
  const Rgb8 color{scene.frame.r, scene.frame.g, scene.frame.b};
  return effectPixel(scene.light.effect, color, scene.light.speed, scene.ms, x, y, l.width, l.height);
}

bool sceneMoves(const Scene& scene) {
  return scene.haveColor && !scene.off && !scene.frame.stale && effectMoves(scene.light.effect);
}

uint8_t capBrightness(uint8_t level, uint8_t cap) {
  if (cap == 0) cap = 1;
  if (level == 0) level = 1;
  return static_cast<uint8_t>(1 + (static_cast<uint32_t>(level) * (cap - 1) + 127) / 255);
}

namespace {
constexpr uint8_t STORED_VERSION = 1;
}

void encodeSettings(const Settings& s, uint8_t out[STORED_SIZE]) {
  memset(out, 0, STORED_SIZE);
  out[0] = STORED_VERSION;
  out[1] = static_cast<uint8_t>(s.output);
  out[2] = static_cast<uint8_t>(s.pixelPin);
  out[3] = static_cast<uint8_t>(s.count);
  out[4] = static_cast<uint8_t>(s.count >> 8);
  out[5] = static_cast<uint8_t>(s.width);
  out[6] = static_cast<uint8_t>(s.width >> 8);
  out[7] = static_cast<uint8_t>((s.serpentine ? 1 : 0) | (s.commonAnode ? 2 : 0));
  out[8] = static_cast<uint8_t>(s.order);
  out[9] = static_cast<uint8_t>(s.redPin);
  out[10] = static_cast<uint8_t>(s.greenPin);
  out[11] = static_cast<uint8_t>(s.bluePin);
  out[12] = s.maxBrightness;
  out[13] = static_cast<uint8_t>(s.buttonPin);
}

bool decodeSettings(const uint8_t* data, size_t length, Settings& out) {
  if (data == nullptr || length != STORED_SIZE || data[0] != STORED_VERSION) return false;
  if (data[1] > static_cast<uint8_t>(Output::Rgb) || data[8] > static_cast<uint8_t>(ColorOrder::BGR)) return false;
  Settings s;
  s.output = static_cast<Output>(data[1]);
  s.pixelPin = static_cast<int8_t>(data[2]);
  s.count = static_cast<uint16_t>(data[3] | (data[4] << 8));
  s.width = static_cast<uint16_t>(data[5] | (data[6] << 8));
  s.serpentine = (data[7] & 1) != 0;
  s.commonAnode = (data[7] & 2) != 0;
  s.order = static_cast<ColorOrder>(data[8]);
  s.redPin = static_cast<int8_t>(data[9]);
  s.greenPin = static_cast<int8_t>(data[10]);
  s.bluePin = static_cast<int8_t>(data[11]);
  s.maxBrightness = data[12];
  s.buttonPin = static_cast<int8_t>(data[13]);
  if (s.count < 1 || s.count > MAX_PIXELS || s.maxBrightness < 1) return false;
  out = s;
  return true;
}

namespace {
const char* const ORDER_NAMES[] = {"rgb", "rbg", "grb", "gbr", "brg", "bgr"};
}

const char* orderName(ColorOrder order) { return ORDER_NAMES[static_cast<int>(order)]; }

void describe(const Settings& s, char* out, size_t size) {
  char button[24];
  if (s.buttonPin >= 0) {
    snprintf(button, sizeof(button), "button on GPIO %d", s.buttonPin);
  } else {
    snprintf(button, sizeof(button), "no button");
  }
  switch (s.output) {
    case Output::Pixels: {
      char shape[40];
      if (s.width == 0 || s.width >= s.count) {
        snprintf(shape, sizeof(shape), "in a strip");
      } else {
        snprintf(shape, sizeof(shape), "in rows of %u%s", s.width, s.serpentine ? ", serpentine" : "");
      }
      snprintf(out, size, "%u LED%s on GPIO %d %s, order %s, brightness cap %u, %s", s.count, s.count == 1 ? "" : "s",
               s.pixelPin, shape, orderName(s.order), s.maxBrightness, button);
      break;
    }
    case Output::Rgb:
      snprintf(out, size, "an RGB LED on GPIO %d, %d, %d, common %s, brightness cap %u, %s", s.redPin, s.greenPin,
               s.bluePin, s.commonAnode ? "anode" : "cathode", s.maxBrightness, button);
      break;
    default:
      snprintf(out, size, "no light set up, %s", button);
      break;
  }
}

void exportCommands(const Settings& s, char* out, size_t size) {
  char light[64];
  switch (s.output) {
    case Output::Pixels:
      snprintf(light, sizeof(light), "pixels %d %u %u; order %s; serpentine %s", s.pixelPin, s.count, s.width,
               orderName(s.order), s.serpentine ? "on" : "off");
      break;
    case Output::Rgb:
      snprintf(light, sizeof(light), "rgb %d %d %d; anode %s", s.redPin, s.greenPin, s.bluePin,
               s.commonAnode ? "on" : "off");
      break;
    default:
      snprintf(out, size, "none");
      return;
  }
  char button[16];
  if (s.buttonPin >= 0) {
    snprintf(button, sizeof(button), "%d", s.buttonPin);
  } else {
    snprintf(button, sizeof(button), "none");
  }
  snprintf(out, size, "%s; max %u; button %s", light, s.maxBrightness, button);
}

namespace {

constexpr int MAX_TOKENS = 6;

// Splits a line into lowercase words. Returns how many.
int tokenize(const char* line, char words[MAX_TOKENS][24]) {
  int n = 0;
  const char* p = line;
  while (*p && n < MAX_TOKENS) {
    while (*p && isspace(static_cast<unsigned char>(*p))) p++;
    if (!*p) break;
    size_t len = 0;
    while (*p && !isspace(static_cast<unsigned char>(*p))) {
      if (len < 23) words[n][len++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
      p++;
    }
    words[n][len] = '\0';
    n++;
  }
  return n;
}

bool number(const char* word, int low, int high, int& out) {
  if (!*word) return false;
  char* end = nullptr;
  const long v = strtol(word, &end, 10);
  if (*end != '\0' || v < low || v > high) return false;
  out = static_cast<int>(v);
  return true;
}

bool onOff(const char* word, bool& out) {
  if (strcmp(word, "on") == 0) return out = true, true;
  if (strcmp(word, "off") == 0) return out = false, true;
  return false;
}

const char HELP[] =
    "Commands (changes take effect on save):\n"
    "  show                     the settings in use, and changes waiting for save\n"
    "  export                   the settings in use, as commands\n"
    "  boards                   presets for this chip\n"
    "  board <name>             start from a preset\n"
    "  pixels <pin> <count> [width]  addressable LEDs: a strip, or a grid with width LEDs per row\n"
    "  order <rgb|grb|...>      the color order of the LEDs\n"
    "  serpentine <on|off>      grid rows that zigzag\n"
    "  rgb <red> <green> <blue> a plain RGB LED on three pins\n"
    "  anode <on|off>           a common-anode RGB LED\n"
    "  max <1-255>              brightness cap\n"
    "  button <pin|none>        a button to ground that turns the light off and on\n"
    "  save                     keep the changes and restart\n"
    "  cancel                   drop changes not yet saved\n"
    "  reset                    forget the settings, then restart\n"
    "  restart\n"
    "Finding your LEDs (nothing is saved):\n"
    "  pins                     the pins that can drive LEDs on this chip\n"
    "  test <pin> <count> <rrggbb>  light that color on the first count LEDs of a pin\n"
    "  test off                 stop the test\n"
    "  about                    chip, firmware, name, and whether it is set up";

constexpr char SAVE_HINT[] = " Type save to keep it.";

bool hexColor(const char* word, Rgb8& out) {
  if (strlen(word) != 6) return false;
  for (const char* p = word; *p; p++) {
    if (!isxdigit(static_cast<unsigned char>(*p))) return false;
  }
  const unsigned long v = strtoul(word, nullptr, 16);
  out = {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
  return true;
}

}  // namespace

Action handleCommand(const char* line, const char* chip, const Settings& running, Settings& staged, char* out,
                     size_t size, TestRequest* test) {
  char w[MAX_TOKENS][24] = {};
  const int n = tokenize(line, w);
  out[0] = '\0';
  if (n == 0) return Action::None;
  const char* cmd = w[0];
  int a = 0, b = 0, c = 0;

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    snprintf(out, size, "%s", HELP);
  } else if (strcmp(cmd, "about") == 0 && n == 1) {
    return Action::About;  // the caller knows the firmware version and the name
  } else if (strcmp(cmd, "pins") == 0 && n == 1) {
    size_t at = static_cast<size_t>(snprintf(out, size, "pins"));
    for (int pin = 0; pin < 64 && at < size; pin++) {
      if (pinUsable(chip, pin, true) && pin != running.buttonPin) {
        at += static_cast<size_t>(snprintf(out + at, size - at, " %d", pin));
      }
    }
  } else if (strcmp(cmd, "test") == 0 && n == 2 && strcmp(w[1], "off") == 0) {
    snprintf(out, size, "test off");
    return Action::TestOff;
  } else if (strcmp(cmd, "test") == 0 && n == 4 && number(w[1], 0, 127, a) && number(w[2], 1, MAX_PIXELS, b)) {
    Rgb8 color{};
    if (!hexColor(w[3], color)) {
      snprintf(out, size, "The color is six hex digits, such as ff0000 for red.");
    } else if (!pinUsable(chip, a, true)) {
      snprintf(out, size, "GPIO %d cannot drive LEDs on %s.", a, chip);
    } else if (a == running.buttonPin) {
      snprintf(out, size, "GPIO %d is the button.", a);
    } else {
      if (test != nullptr) *test = {a, b, color};
      snprintf(out, size, "test pin=%d count=%d color=%s", a, b, w[3]);
      return Action::Test;
    }
  } else if (strcmp(cmd, "show") == 0) {
    char now[160], next[160];
    describe(running, now, sizeof(now));
    if (staged == running) {
      snprintf(out, size, "In use: %s.", now);
    } else {
      describe(staged, next, sizeof(next));
      snprintf(out, size, "In use: %s.\nWaiting for save: %s.", now, next);
    }
  } else if (strcmp(cmd, "export") == 0 && n == 1) {
    size_t at = static_cast<size_t>(snprintf(out, size, "export "));
    if (at < size) exportCommands(running, out + at, size - at);
  } else if (strcmp(cmd, "boards") == 0) {
    size_t at = static_cast<size_t>(snprintf(out, size, "Presets for %s:", chip));
    for (size_t i = 0; i < PRESET_COUNT && at < size; i++) {
      if (strcmp(PRESETS[i].chip, chip) != 0) continue;
      at += static_cast<size_t>(snprintf(out + at, size - at, "\n  %-16s %s%s", PRESETS[i].name, PRESETS[i].board,
                                         PRESETS[i].tested ? "" : " (untested)"));
    }
  } else if (strcmp(cmd, "board") == 0 && n == 2) {
    const Preset* p = findPreset(w[1]);
    if (p == nullptr || strcmp(p->chip, chip) != 0) {
      snprintf(out, size, "No preset '%s' for %s. Type boards for the list.", w[1], chip);
    } else {
      staged = p->settings;
      snprintf(out, size, "Preset %s: %s.%s", p->name, p->board, SAVE_HINT);
    }
  } else if (strcmp(cmd, "pixels") == 0 && (n == 3 || n == 4) && number(w[1], 0, 127, a) &&
             number(w[2], 1, MAX_PIXELS, b) && (n == 3 || number(w[3], 0, MAX_PIXELS, c))) {
    if (!pinUsable(chip, a, true)) {
      snprintf(out, size, "GPIO %d cannot drive LEDs on %s.", a, chip);
    } else if (c > b) {
      snprintf(out, size, "A row cannot be wider than the %d LEDs.", b);
    } else {
      staged.output = Output::Pixels;
      staged.pixelPin = static_cast<int8_t>(a);
      staged.count = static_cast<uint16_t>(b);
      staged.width = static_cast<uint16_t>(c);
      snprintf(out, size, "%d LED%s on GPIO %d.%s", b, b == 1 ? "" : "s", a, SAVE_HINT);
    }
  } else if (strcmp(cmd, "order") == 0 && n == 2) {
    int found = -1;
    for (int i = 0; i < 6; i++) {
      if (strcmp(w[1], ORDER_NAMES[i]) == 0) found = i;
    }
    if (found < 0) {
      snprintf(out, size, "The order is one of rgb, rbg, grb, gbr, brg, bgr.");
    } else {
      staged.order = static_cast<ColorOrder>(found);
      snprintf(out, size, "Color order %s.%s", w[1], SAVE_HINT);
    }
  } else if (strcmp(cmd, "serpentine") == 0 && n == 2 && onOff(w[1], staged.serpentine)) {
    snprintf(out, size, "Serpentine rows %s.%s", w[1], SAVE_HINT);
  } else if (strcmp(cmd, "rgb") == 0 && n == 4 && number(w[1], 0, 127, a) && number(w[2], 0, 127, b) &&
             number(w[3], 0, 127, c)) {
    if (!pinUsable(chip, a, true) || !pinUsable(chip, b, true) || !pinUsable(chip, c, true)) {
      snprintf(out, size, "Each of the three pins must be able to drive an LED on %s.", chip);
    } else if (a == b || b == c || a == c) {
      snprintf(out, size, "The RGB LED needs three different pins.");
    } else {
      staged.output = Output::Rgb;
      staged.redPin = static_cast<int8_t>(a);
      staged.greenPin = static_cast<int8_t>(b);
      staged.bluePin = static_cast<int8_t>(c);
      snprintf(out, size, "An RGB LED on GPIO %d, %d, %d.%s", a, b, c, SAVE_HINT);
    }
  } else if (strcmp(cmd, "anode") == 0 && n == 2 && onOff(w[1], staged.commonAnode)) {
    snprintf(out, size, "Common anode %s.%s", w[1], SAVE_HINT);
  } else if (strcmp(cmd, "max") == 0 && n == 2 && number(w[1], 1, 255, a)) {
    staged.maxBrightness = static_cast<uint8_t>(a);
    snprintf(out, size, "Brightness cap %d.%s", a, SAVE_HINT);
  } else if (strcmp(cmd, "button") == 0 && n == 2 && strcmp(w[1], "none") == 0) {
    staged.buttonPin = -1;
    snprintf(out, size, "No button.%s", SAVE_HINT);
  } else if (strcmp(cmd, "button") == 0 && n == 2 && number(w[1], 0, 127, a)) {
    if (!pinUsable(chip, a, false)) {
      snprintf(out, size, "GPIO %d cannot read a button on %s.", a, chip);
    } else {
      staged.buttonPin = static_cast<int8_t>(a);
      snprintf(out, size, "Button on GPIO %d.%s", a, SAVE_HINT);
    }
  } else if (strcmp(cmd, "save") == 0 && n == 1) {
    const char* why = problem(chip, staged);
    if (why != nullptr) {
      snprintf(out, size, "Not saved: %s.", why);
    } else if (staged == running) {
      snprintf(out, size, "Nothing to save.");
    } else {
      snprintf(out, size, "Saved. Restarting.");
      return Action::Save;
    }
  } else if (strcmp(cmd, "cancel") == 0 && n == 1) {
    staged = running;
    snprintf(out, size, "Changes dropped.");
  } else if (strcmp(cmd, "reset") == 0 && n == 1) {
    snprintf(out, size, "Settings forgotten. Restarting.");
    return Action::Reset;
  } else if (strcmp(cmd, "restart") == 0 && n == 1) {
    snprintf(out, size, "Restarting.");
    return Action::Restart;
  } else {
    // A known command with the wrong arguments gets its line from the help.
    char key[28];
    snprintf(key, sizeof(key), "\n  %s ", cmd);
    const char* found = strstr(HELP, key);
    if (found != nullptr) {
      const char* start = found + 3;
      const char* end = strchr(start, '\n');
      const int length = static_cast<int>(end != nullptr ? end - start : strlen(start));
      snprintf(out, size, "Usage: %.*s", length, start);
    } else {
      snprintf(out, size, "Not a command: %s. Type help for the list.", line);
    }
  }
  return Action::None;
}

}  // namespace cheerlights

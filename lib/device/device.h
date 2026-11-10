// What the light is wired to, how pixels are laid out, and the USB console that changes it.
// No hardware dependencies, so the native tests cover all of it.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "profile.h"

namespace cheerlights {

// Screen is the LilyGO T-Display's built-in 1.14 inch screen, the one screen this firmware drives.
enum class Output : uint8_t { None = 0, Pixels = 1, Rgb = 2, Screen = 3 };

// How the light gets the color: from a phone over Bluetooth (docs/ble-profile.md), or by itself over Wi-Fi
// (docs/wifi.md). A light is one or the other, chosen at setup.
enum class Link : uint8_t { Bluetooth = 0, Wifi = 1 };

// The order an addressable LED takes its three colors in. WS2812 is usually GRB, WS2811 often RGB.
enum class ColorOrder : uint8_t { RGB = 0, RBG = 1, GRB = 2, GBR = 3, BRG = 4, BGR = 5 };

struct Settings {
  Output output = Output::None;
  // Addressable LEDs (WS2812, SK6812, WS2811) on one data pin.
  int8_t pixelPin = -1;
  uint16_t count = 1;
  uint16_t width = 0;  // pixels per row of a grid; 0 means a strip, one row of all of them
  bool serpentine = false;
  ColorOrder order = ColorOrder::GRB;
  // A plain RGB LED on three pins.
  int8_t redPin = -1, greenPin = -1, bluePin = -1;
  bool commonAnode = false;
  // The most brightness 255 from the phone can reach, for boards that run hot or strips that draw a lot.
  uint8_t maxBrightness = 64;
  // A button to ground that turns the light off and on, or on a screen steps through the animations; -1 for none.
  int8_t buttonPin = -1;
  Link link = Link::Bluetooth;
  // A screen only.
  bool label = true;  // the color's name on the screen
  bool flip = false;  // the picture upside down, for a board stood the other way up
};

bool operator==(const Settings& a, const Settings& b);
inline bool operator!=(const Settings& a, const Settings& b) { return !(a == b); }

constexpr uint16_t MAX_PIXELS = 512;

// A named starting point for a board with known wiring.
struct Preset {
  const char* name;
  const char* chip;  // the build's chip, such as "esp32s3"
  const char* board;
  Settings settings;
  bool tested;  // run on the real board, not only built for it
};
extern const Preset PRESETS[];
extern const size_t PRESET_COUNT;

// What the light says it is: the description of the preset its wiring matches on this chip, or "Custom".
// The link, label, and flip are choices, not wiring, so they do not change the answer.
const char* boardName(const Settings& s, const char* chip);

const Preset* findPreset(const char* name);

// A fresh device drives no pin until it is set up, because nothing tells it what is wired where.
bool isSetUp(const Settings& s);

// Whether a GPIO can drive an LED (output) or read a button (input) on this chip, leaving out the pins
// for flash, PSRAM, the USB port, and the serial console.
bool pinUsable(const char* chip, int pin, bool output);

// Why a set of settings cannot run on this chip, or nullptr when it can.
const char* problem(const char* chip, const Settings& s);

// Whether this chip has Wi-Fi; the ESP32-H2 does not.
bool chipHasWifi(const char* chip);

// The animations this light has, which depend on its output.
const EffectInfo* effectsFor(Output output, size_t& count);

// Layout: column and row of pixel i, with serpentine rows reversed on odd rows.
struct Layout {
  int width, height;
};
Layout layoutOf(const Settings& s);
void pixelXY(const Settings& s, int index, int& x, int& y);
int pixelCount(const Settings& s);

// The pixels turned off on a stale color: the four corners of a grid, the two ends of a strip.
bool isMarker(const Settings& s, int index);

// The pixel lit before any color arrives, so a fresh device shows it is alive.
int alivePixel(const Settings& s);

// The value pixel i is sent before the brightness cap, for everything the device can be showing. Colors are LED
// values already (ledBase), so the output sends them as they are.
struct Scene {
  bool haveColor;
  Frame frame;
  bool off;
  Light light;
  uint32_t ms;
};
Rgb8 scenePixel(const Settings& s, const Scene& scene, int index);
bool sceneMoves(const Scene& scene);

// Light brightness 1 to 255 becomes 1 to the cap.
uint8_t capBrightness(uint8_t level, uint8_t cap);

// A channel value (0 to 255) at a brightness level (1 to 255), in 1/256ths of an output step, so nothing is
// rounded before the last moment. A light capped low has few steps per channel (16 at a cap of 16), and rounding
// twice turned the dim end of a breath red: orange's green reached zero while its red was still lit.
uint16_t outputLevel(uint8_t channel, uint8_t level);

// The nearest whole step, for a light that is not moving.
uint8_t roundLevel(uint16_t value);

// The step to send this frame while a light moves. The fraction left over carries to the next frame in residue,
// so over a few frames each channel averages exactly what it should, and the hue holds at any brightness.
uint8_t ditherLevel(uint16_t value, uint8_t& residue);

// Blinking to say "this one": white and off, a quarter second each, for IDENTIFY_MS, so an owner with several
// lights can tell which one the app or the page is talking to (Hans, 2026-11-09).
constexpr uint32_t IDENTIFY_MS = 3000;
bool identifyLit(uint32_t elapsedMs);

// Settings kept in flash, versioned so a later layout is read safely.
constexpr size_t STORED_SIZE = 16;
void encodeSettings(const Settings& s, uint8_t out[STORED_SIZE]);
bool decodeSettings(const uint8_t* data, size_t length, Settings& out);

const char* orderName(ColorOrder order);
void describe(const Settings& s, char* out, size_t size);

// The settings as console commands on one line, separated by "; ", or "none" when not set up.
// Sending each command and then save sets up a fresh board the same way, so the install page can
// keep a copy and put it back after the board is erased.
void exportCommands(const Settings& s, char* out, size_t size);

// A test color on a pin, shown until the next test or `test off` and never saved, so a person or the
// install page can find which pin the LEDs are on and which color order they take.
// The color goes out in the order given, R first, so a red that shows green means the LEDs take green first.
struct TestRequest {
  int pin;
  int count;
  Rgb8 color;
  bool split;   // a second color on the second half, to compare two colors side by side
  Rgb8 second;
};

// The USB console. Changes go to `staged` and take effect on save. A reply, help included, fits in REPLY_SIZE.
constexpr size_t REPLY_SIZE = 2400;
enum class Action { None, Save, Reset, Restart, About, Test, TestOff };
Action handleCommand(const char* line, const char* chip, const Settings& running, Settings& staged, char* out,
                     size_t size, TestRequest* test = nullptr);

}  // namespace cheerlights

// What the light is wired to, how pixels are laid out, and the USB console that changes it.
// No hardware dependencies, so the native tests cover all of it.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "profile.h"

namespace cheerlights {

enum class Output : uint8_t { None = 0, Pixels = 1, Rgb = 2 };

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
  // A button to ground that turns the light off and on; -1 for none.
  int8_t buttonPin = -1;
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

const Preset* findPreset(const char* name);

// A fresh device drives no pin until it is set up, because nothing tells it what is wired where.
bool isSetUp(const Settings& s);

// Whether a GPIO can drive an LED (output) or read a button (input) on this chip, leaving out the pins
// for flash, PSRAM, the USB port, and the serial console.
bool pinUsable(const char* chip, int pin, bool output);

// Why a set of settings cannot run on this chip, or nullptr when it can.
const char* problem(const char* chip, const Settings& s);

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

// The color of pixel i before the brightness cap, for everything the device can be showing.
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
};

// The USB console. Changes go to `staged` and take effect on save.
enum class Action { None, Save, Reset, Restart, About, Test, TestOff };
Action handleCommand(const char* line, const char* chip, const Settings& running, Settings& staged, char* out,
                     size_t size, TestRequest* test = nullptr);

}  // namespace cheerlights

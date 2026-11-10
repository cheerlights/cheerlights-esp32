#include <device.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

using namespace cheerlights;

void setUp() {}
void tearDown() {}

static const char* const CHIPS[] = {"esp32", "esp32s3", "esp32c3", "esp32c5", "esp32c6", "esp32h2"};

// A fresh device is not set up and drives no pin; every chip still has at least one preset to pick.
static void test_a_fresh_device_is_not_set_up_and_every_chip_has_presets() {
  const Settings fresh{};
  TEST_ASSERT_FALSE(isSetUp(fresh));
  TEST_ASSERT_EQUAL(0, pixelCount(fresh));
  for (const char* chip : CHIPS) {
    TEST_ASSERT_NULL_MESSAGE(problem(chip, fresh), chip);
    int presets = 0;
    for (size_t i = 0; i < PRESET_COUNT; i++) presets += strcmp(PRESETS[i].chip, chip) == 0 ? 1 : 0;
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, presets, chip);
  }
  TEST_ASSERT_TRUE(isSetUp(findPreset("s3-matrix")->settings));
}

static void test_every_preset_runs_on_its_chip_and_names_are_unique() {
  for (size_t i = 0; i < PRESET_COUNT; i++) {
    const Preset& p = PRESETS[i];
    TEST_ASSERT_NULL_MESSAGE(problem(p.chip, p.settings), p.name);
    TEST_ASSERT_TRUE_MESSAGE(strlen(p.name) <= 16, p.name);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&p, findPreset(p.name), p.name);
  }
}

static void test_pins_for_flash_usb_and_the_console_are_refused() {
  for (int pin : {6, 7, 8, 9, 10, 11, 1, 3}) TEST_ASSERT_FALSE(pinUsable("esp32", pin, true));
  TEST_ASSERT_FALSE(pinUsable("esp32", 34, true));  // input only
  TEST_ASSERT_TRUE(pinUsable("esp32", 34, false));
  TEST_ASSERT_TRUE(pinUsable("esp32", 27, true));
  for (int pin : {19, 20, 26, 32, 43, 44, 49}) TEST_ASSERT_FALSE(pinUsable("esp32s3", pin, true));
  TEST_ASSERT_TRUE(pinUsable("esp32s3", 14, true));
  TEST_ASSERT_TRUE(pinUsable("esp32s3", 48, true));
  for (int pin : {11, 17, 18, 19, 20, 21, 22}) TEST_ASSERT_FALSE(pinUsable("esp32c3", pin, true));
  TEST_ASSERT_TRUE(pinUsable("esp32c3", 10, true));
  for (int pin : {12, 13, 16, 17, 24, 31}) TEST_ASSERT_FALSE(pinUsable("esp32c6", pin, true));
  for (int pin : {15, 21, 23, 24, 26, 27, 28}) TEST_ASSERT_FALSE(pinUsable("esp32h2", pin, true));
  for (int pin : {11, 12, 13, 14, 16, 22, 29}) TEST_ASSERT_FALSE(pinUsable("esp32c5", pin, true));
  TEST_ASSERT_TRUE(pinUsable("esp32c5", 27, true));
  TEST_ASSERT_FALSE(pinUsable("esp32s3", -1, true));
  TEST_ASSERT_FALSE(pinUsable("esp32s2", 5, true));  // no Bluetooth, so no table
}

static Settings strip(int count, int width, bool serpentine) {
  Settings s;
  s.output = Output::Pixels;
  s.pixelPin = 5;
  s.count = static_cast<uint16_t>(count);
  s.width = static_cast<uint16_t>(width);
  s.serpentine = serpentine;
  return s;
}

static void test_layout_of_a_strip_a_grid_and_a_serpentine_grid() {
  int x, y;
  const Settings line = strip(60, 0, false);
  TEST_ASSERT_EQUAL(60, layoutOf(line).width);
  TEST_ASSERT_EQUAL(1, layoutOf(line).height);
  pixelXY(line, 59, x, y);
  TEST_ASSERT_EQUAL(59, x);
  TEST_ASSERT_EQUAL(0, y);

  const Settings grid = strip(64, 8, false);
  pixelXY(grid, 9, x, y);
  TEST_ASSERT_EQUAL(1, x);
  TEST_ASSERT_EQUAL(1, y);

  const Settings zigzag = strip(64, 8, true);
  pixelXY(zigzag, 8, x, y);  // first pixel of row 1 is at its right end
  TEST_ASSERT_EQUAL(7, x);
  TEST_ASSERT_EQUAL(1, y);
  pixelXY(zigzag, 16, x, y);
  TEST_ASSERT_EQUAL(0, x);

  const Settings partial = strip(10, 4, false);  // rows of 4, 4, 2
  TEST_ASSERT_EQUAL(3, layoutOf(partial).height);

  Settings rgb;
  rgb.output = Output::Rgb;
  TEST_ASSERT_EQUAL(1, pixelCount(rgb));
  TEST_ASSERT_EQUAL(0, pixelCount(Settings{}));
}

static void test_stale_markers_are_grid_corners_and_strip_ends() {
  const Settings grid = strip(64, 8, false);
  int corners = 0;
  for (int i = 0; i < 64; i++) corners += isMarker(grid, i) ? 1 : 0;
  TEST_ASSERT_EQUAL(4, corners);
  TEST_ASSERT_TRUE(isMarker(grid, 0));
  TEST_ASSERT_TRUE(isMarker(grid, 7));
  TEST_ASSERT_TRUE(isMarker(grid, 56));
  TEST_ASSERT_TRUE(isMarker(grid, 63));

  const Settings line = strip(30, 0, false);
  TEST_ASSERT_TRUE(isMarker(line, 0));
  TEST_ASSERT_TRUE(isMarker(line, 29));
  TEST_ASSERT_FALSE(isMarker(line, 15));
  TEST_ASSERT_FALSE(isMarker(strip(1, 0, false), 0));  // a single LED only dims
}

static void test_the_alive_pixel_is_in_the_middle() {
  TEST_ASSERT_EQUAL(36, alivePixel(strip(64, 8, false)));  // the 8x8 wearable's pixel, as before
  TEST_ASSERT_EQUAL(30, alivePixel(strip(60, 0, false)));
  TEST_ASSERT_EQUAL(0, alivePixel(strip(1, 0, false)));
  TEST_ASSERT_EQUAL(12, alivePixel(strip(25, 5, false)));
}

static void test_scene_shows_nothing_off_alive_stale_and_fresh() {
  const Settings grid = strip(64, 8, false);
  Scene scene{};
  TEST_ASSERT_EQUAL_UINT8(40, scenePixel(grid, scene, 36).r);
  TEST_ASSERT_EQUAL_UINT8(0, scenePixel(grid, scene, 0).r);

  scene.haveColor = true;
  scene.frame.r = 255;
  scene.frame.g = 0;
  scene.frame.b = 0;
  TEST_ASSERT_EQUAL_UINT8(255, scenePixel(grid, scene, 0).r);

  scene.frame.stale = true;
  TEST_ASSERT_EQUAL_UINT8(0, scenePixel(grid, scene, 0).r);   // corner off
  TEST_ASSERT_EQUAL_UINT8(85, scenePixel(grid, scene, 9).r);  // a third
  scene.light.effect = EFFECT_RAINBOW;
  TEST_ASSERT_FALSE(sceneMoves(scene));  // a stale color never animates

  scene.frame.stale = false;
  TEST_ASSERT_TRUE(sceneMoves(scene));
  scene.off = true;
  TEST_ASSERT_EQUAL_UINT8(0, scenePixel(grid, scene, 9).r);
  TEST_ASSERT_FALSE(sceneMoves(scene));
}

// Tester report, 2026-11-09: pink looked almost white on the LEDs. A palette color goes out as its picked LED
// value, whatever the frame's hex says; any other color goes through gamma.
static void test_scene_sends_led_values() {
  const Settings grid = strip(64, 8, false);
  Scene scene{};
  scene.haveColor = true;
  scene.frame.paletteIndex = 10;  // pink
  scene.frame.r = 0xff;
  scene.frame.g = 0xc0;
  scene.frame.b = 0xcb;
  Rgb8 p = scenePixel(grid, scene, 9);
  TEST_ASSERT_TRUE(p.r == 0xff && p.g == 0x24 && p.b == 0x54);
  scene.frame.paletteIndex = 1;  // green, 008000 on a screen, full on an LED
  p = scenePixel(grid, scene, 9);
  TEST_ASSERT_TRUE(p.r == 0 && p.g == 0xff && p.b == 0);

  scene.frame.paletteIndex = PALETTE_NONE;  // a color from the wheel
  scene.frame.r = 0xff;
  scene.frame.g = 0xc0;
  scene.frame.b = 0xcb;
  p = scenePixel(grid, scene, 9);
  TEST_ASSERT_EQUAL_UINT8(0xff, p.r);
  TEST_ASSERT_INT_WITHIN(2, 115, p.g);
  TEST_ASSERT_INT_WITHIN(2, 135, p.b);

  scene.frame.paletteIndex = 7;  // magenta
  scene.frame.stale = true;
  p = scenePixel(grid, scene, 9);
  TEST_ASSERT_TRUE(p.r == 85 && p.g == 0 && p.b == 85);  // a third of the LED value
  scene.frame.stale = false;

  scene.light.effect = EFFECT_RAINBOW;  // draws its own colors, so they go through gamma
  scene.ms = 1234;
  int x, y;
  pixelXY(grid, 20, x, y);
  const Rgb8 raw = effectPixel(EFFECT_RAINBOW, ledBase(scene.frame), scene.light.speed, scene.ms, x, y, 8, 8);
  const Rgb8 led = ledColor(raw);
  p = scenePixel(grid, scene, 20);
  TEST_ASSERT_TRUE(p.r == led.r && p.g == led.g && p.b == led.b);

  Scene fresh{};
  TEST_ASSERT_EQUAL_UINT8(40, scenePixel(grid, fresh, 36).r);  // the alive pixel stays visible
}

// Hans, 2026-11-09: breathe turned orange red at the dim end and stuttered, on the Matrix at a cap of 16. The old
// path rounded twice, so orange's green was off for a fifth of each breath and red had 15 steps. Here a whole breath
// runs at 200 frames a second through the same functions the output uses.
static void test_a_dim_breath_keeps_its_hue_and_fades_smoothly() {
  const Settings grid = strip(64, 8, false);
  Scene scene{};
  scene.haveColor = true;
  scene.frame.paletteIndex = 9;  // orange, ff4000 on an LED
  scene.light.effect = EFFECT_BREATHE;
  const uint8_t level = capBrightness(255, 16);
  uint8_t residue[3] = {0, 0, 0};
  const int window = 10;  // 50 ms, well under what the eye resolves
  int greenOffWindows = 0, windows = 0;
  double worst = 0;
  uint32_t sum[3] = {0, 0, 0};
  int lastRedTenths = -1, redLevels = 0;
  for (uint32_t ms = 0; ms < 4800; ms += 5) {  // one breath at speed 128 is about 3.0 s
    scene.ms = ms;
    const Rgb8 p = scenePixel(grid, scene, 9);
    const uint8_t in[3] = {p.r, p.g, p.b};
    for (int k = 0; k < 3; k++) sum[k] += ditherLevel(outputLevel(in[k], level), residue[k]);
    if ((ms / 5 + 1) % window == 0) {
      windows++;
      if (sum[1] == 0 && sum[0] > 0) greenOffWindows++;
      if (sum[0] > 0) {
        const double ratio = static_cast<double>(sum[1]) / sum[0];  // 0x40 / 0xff = 0.25 for this orange
        const double off = ratio > 0.251 ? ratio - 0.251 : 0.251 - ratio;
        if (off > worst) worst = off;
      }
      const int tenths = static_cast<int>(sum[0] * 10 / window);
      if (tenths != lastRedTenths) redLevels++;
      lastRedTenths = tenths;
      sum[0] = sum[1] = sum[2] = 0;
    }
  }
  TEST_ASSERT_EQUAL(0, greenOffWindows);
  TEST_ASSERT_TRUE_MESSAGE(worst < 0.12, "orange's green stays near a quarter of its red through the breath");
  TEST_ASSERT_TRUE_MESSAGE(redLevels > 60, "the fade has far more than 15 steps");

  // A light that is not moving rounds once, so a still color never shimmers.
  TEST_ASSERT_EQUAL_UINT8(16, roundLevel(outputLevel(255, 16)));
  TEST_ASSERT_EQUAL_UINT8(4, roundLevel(outputLevel(0x40, 16)));
  TEST_ASSERT_EQUAL_UINT8(255, roundLevel(outputLevel(255, 255)));
  TEST_ASSERT_EQUAL_UINT8(0, roundLevel(outputLevel(0, 255)));
  // Dithering never runs past the top and averages exactly.
  uint8_t r = 255;
  TEST_ASSERT_EQUAL_UINT8(255, ditherLevel(outputLevel(255, 255), r));
  r = 0;
  uint32_t total = 0;
  for (int i = 0; i < 256; i++) total += ditherLevel(outputLevel(0x24, 16), r);  // pink's green: 2.26 steps
  TEST_ASSERT_UINT32_WITHIN(1, 578, total);
}

static void test_brightness_cap_maps_1_to_255_onto_1_to_cap() {
  TEST_ASSERT_EQUAL_UINT8(16, capBrightness(255, 16));
  TEST_ASSERT_EQUAL_UINT8(1, capBrightness(1, 16));
  TEST_ASSERT_EQUAL_UINT8(1, capBrightness(0, 16));
  TEST_ASSERT_EQUAL_UINT8(255, capBrightness(255, 255));
  TEST_ASSERT_EQUAL_UINT8(6, capBrightness(77, 16));  // what firmware 0.2 showed for 77
}

static void test_settings_round_trip_and_bad_data_is_refused() {
  for (size_t i = 0; i < PRESET_COUNT; i++) {
    uint8_t b[STORED_SIZE];
    encodeSettings(PRESETS[i].settings, b);
    Settings back;
    TEST_ASSERT_TRUE_MESSAGE(decodeSettings(b, STORED_SIZE, back), PRESETS[i].name);
    TEST_ASSERT_TRUE_MESSAGE(back == PRESETS[i].settings, PRESETS[i].name);
  }
  Settings rgb;
  rgb.output = Output::Rgb;
  rgb.redPin = 25;
  rgb.greenPin = 26;
  rgb.bluePin = 32;
  rgb.commonAnode = true;
  uint8_t b[STORED_SIZE];
  encodeSettings(rgb, b);
  Settings back;
  TEST_ASSERT_TRUE(decodeSettings(b, STORED_SIZE, back));
  TEST_ASSERT_TRUE(back == rgb);

  Settings untouched = rgb;
  uint8_t bad[STORED_SIZE];
  memcpy(bad, b, STORED_SIZE);
  bad[0] = 2;  // a later layout
  TEST_ASSERT_FALSE(decodeSettings(bad, STORED_SIZE, untouched));
  TEST_ASSERT_TRUE(untouched == rgb);
  TEST_ASSERT_FALSE(decodeSettings(b, STORED_SIZE - 1, untouched));
  memcpy(bad, b, STORED_SIZE);
  bad[1] = 9;  // an output this firmware does not know
  TEST_ASSERT_FALSE(decodeSettings(bad, STORED_SIZE, untouched));
}

static Action run(const char* line, Settings& staged, const Settings& running, char* out, size_t size,
                  const char* chip = "esp32s3") {
  return handleCommand(line, chip, running, staged, out, size);
}

static void test_console_stages_changes_until_save() {
  const Settings running = findPreset("s3-matrix")->settings;
  Settings staged = running;
  char out[1200];

  TEST_ASSERT_EQUAL(Action::None, run("pixels 5 30", staged, running, out, sizeof(out)));
  TEST_ASSERT_EQUAL(5, staged.pixelPin);
  TEST_ASSERT_EQUAL(30, staged.count);
  TEST_ASSERT_EQUAL(0, staged.width);
  TEST_ASSERT_NOT_NULL(strstr(out, "save"));

  TEST_ASSERT_EQUAL(Action::None, run("ORDER grb", staged, running, out, sizeof(out)));
  TEST_ASSERT_TRUE(staged.order == ColorOrder::GRB);
  TEST_ASSERT_EQUAL(Action::None, run("max 40", staged, running, out, sizeof(out)));
  TEST_ASSERT_EQUAL(Action::None, run("button none", staged, running, out, sizeof(out)));
  TEST_ASSERT_EQUAL(-1, staged.buttonPin);

  TEST_ASSERT_EQUAL(Action::None, run("show", staged, running, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(strstr(out, "Waiting for save: 30 LEDs on GPIO 5 in a strip, order grb, brightness cap 40"));

  TEST_ASSERT_EQUAL(Action::Save, run("save", staged, running, out, sizeof(out)));

  Settings again = running;
  TEST_ASSERT_EQUAL(Action::None, run("save", again, running, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Nothing to save.", out);
}

static void test_console_refuses_bad_pins_and_values() {
  const Settings running = findPreset("c3-devkitm")->settings;
  Settings staged = running;
  char out[1200];
  run("pixels 18 10", staged, running, out, sizeof(out), "esp32c3");  // USB D-
  TEST_ASSERT_TRUE(staged == running);
  TEST_ASSERT_NOT_NULL(strstr(out, "cannot drive"));
  run("pixels 5 0", staged, running, out, sizeof(out), "esp32c3");
  run("pixels 5 513", staged, running, out, sizeof(out), "esp32c3");
  run("pixels 5 10 11", staged, running, out, sizeof(out), "esp32c3");
  run("max 0", staged, running, out, sizeof(out), "esp32c3");
  run("order xyz", staged, running, out, sizeof(out), "esp32c3");
  run("rgb 3 3 4", staged, running, out, sizeof(out), "esp32c3");
  run("button 20", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_TRUE(staged == running);

  // A button on the LED pin is caught at save.
  run("button 8", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_EQUAL(Action::None, run("save", staged, running, out, sizeof(out), "esp32c3"));
  TEST_ASSERT_NOT_NULL(strstr(out, "Not saved"));
}

static void test_console_presets_cancel_reset_and_unknown_commands() {
  const Settings running = findPreset("c3-devkitm")->settings;
  Settings staged = running;
  char out[1200];
  run("boards", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_NOT_NULL(strstr(out, "gledopto-c3"));
  TEST_ASSERT_NULL(strstr(out, "s3-matrix"));  // another chip's preset

  run("board s3-matrix", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_TRUE(staged == running);
  run("board gledopto-c3", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_TRUE(staged == findPreset("gledopto-c3")->settings);

  run("cancel", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_TRUE(staged == running);

  TEST_ASSERT_EQUAL(Action::Reset, run("reset", staged, running, out, sizeof(out), "esp32c3"));
  TEST_ASSERT_EQUAL(Action::Restart, run("restart", staged, running, out, sizeof(out), "esp32c3"));
  TEST_ASSERT_EQUAL(Action::None, run("dance", staged, running, out, sizeof(out), "esp32c3"));
  TEST_ASSERT_NOT_NULL(strstr(out, "help"));
  TEST_ASSERT_EQUAL(Action::None, run("   ", staged, running, out, sizeof(out), "esp32c3"));
  TEST_ASSERT_EQUAL_STRING("", out);

  run("rgb 3 4 5", staged, running, out, sizeof(out), "esp32c3");
  run("anode on", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_TRUE(staged.output == Output::Rgb);
  TEST_ASSERT_TRUE(staged.commonAnode);
  run("show", staged, running, out, sizeof(out), "esp32c3");
  TEST_ASSERT_NOT_NULL(strstr(out, "an RGB LED on GPIO 3, 4, 5, common anode"));
}

static void test_pins_lists_only_usable_pins_and_not_the_button() {
  Settings running = findPreset("c3-devkitm")->settings;  // button on GPIO 9
  Settings staged = running;
  char out[1200];
  TEST_ASSERT_EQUAL(Action::None, run("pins", staged, running, out, sizeof(out), "esp32c3"));
  TEST_ASSERT_EQUAL_STRING("pins 0 1 2 3 4 5 6 7 8 10", out);
  Settings fresh{};
  run("pins", fresh, fresh, out, sizeof(out), "esp32");
  TEST_ASSERT_EQUAL_STRING("pins 0 2 4 5 12 13 14 15 16 17 18 19 21 22 23 25 26 27 32 33", out);
}

static void test_test_lights_a_color_and_refuses_bad_requests() {
  Settings fresh{};
  Settings staged = fresh;
  char out[1200];
  TestRequest t{-1, 0, {0, 0, 0}};
  TEST_ASSERT_EQUAL(Action::Test, handleCommand("test 5 60 FF0000", "esp32s3", fresh, staged, out, sizeof(out), &t));
  TEST_ASSERT_EQUAL(5, t.pin);
  TEST_ASSERT_EQUAL(60, t.count);
  TEST_ASSERT_EQUAL_UINT8(255, t.color.r);
  TEST_ASSERT_EQUAL_UINT8(0, t.color.g);
  TEST_ASSERT_EQUAL_STRING("test pin=5 count=60 color=ff0000", out);
  TEST_ASSERT_FALSE(t.split);
  TEST_ASSERT_TRUE(staged == fresh);  // a test never stages a change

  // Two colors compare side by side: the second takes the second half.
  TEST_ASSERT_EQUAL(Action::Test, handleCommand("test 5 64 8000ff ff00ff", "esp32s3", fresh, staged, out, sizeof(out), &t));
  TEST_ASSERT_TRUE(t.split);
  TEST_ASSERT_EQUAL_UINT8(0x80, t.color.r);
  TEST_ASSERT_EQUAL_UINT8(0xff, t.second.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, t.second.g);
  TEST_ASSERT_EQUAL_STRING("test pin=5 count=64 color=8000ff second=ff00ff", out);
  TEST_ASSERT_EQUAL(Action::None, handleCommand("test 5 64 8000ff pink", "esp32s3", fresh, staged, out, sizeof(out), &t));
  TEST_ASSERT_NOT_NULL(strstr(out, "six hex digits"));

  TEST_ASSERT_EQUAL(Action::TestOff, handleCommand("test off", "esp32s3", fresh, staged, out, sizeof(out), &t));
  TEST_ASSERT_EQUAL(Action::None, handleCommand("test 26 8 ff0000", "esp32s3", fresh, staged, out, sizeof(out), &t));
  TEST_ASSERT_NOT_NULL(strstr(out, "cannot drive"));
  TEST_ASSERT_EQUAL(Action::None, handleCommand("test 5 8 red", "esp32s3", fresh, staged, out, sizeof(out), &t));
  TEST_ASSERT_NOT_NULL(strstr(out, "six hex digits"));
  TEST_ASSERT_EQUAL(Action::None, handleCommand("test 5 0 ff0000", "esp32s3", fresh, staged, out, sizeof(out), &t));
  const Settings matrix = findPreset("s3-matrix")->settings;
  TEST_ASSERT_EQUAL(Action::None, handleCommand("test 0 8 ff0000", "esp32s3", matrix, staged, out, sizeof(out), &t));
  TEST_ASSERT_EQUAL_STRING("GPIO 0 is the button.", out);
  TEST_ASSERT_EQUAL(Action::About, handleCommand("about", "esp32s3", fresh, staged, out, sizeof(out), &t));
}

static void test_a_known_command_with_wrong_arguments_shows_its_usage() {
  Settings s{};
  char out[1200];
  run("pixels 14", s, s, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("Usage: pixels <pin> <count> [width]", out, 35);
  run("test 5", s, s, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("Usage: test <pin> <count> <rrggbb>", out, 34);
  run("board", s, s, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("Usage: board <name>", out, 19);
}

// Export, then each command sent to a fresh board, gives the same settings: what the install page relies on
// to put a board back after it is erased.
static void replay(const char* line, const char* chip, Settings& staged) {
  const Settings fresh{};
  char copy[200], out[300];
  snprintf(copy, sizeof(copy), "%s", line);
  for (char* command = strtok(copy, ";"); command != nullptr; command = strtok(nullptr, ";")) {
    handleCommand(command, chip, fresh, staged, out, sizeof(out));
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "Usage"), command);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "cannot"), command);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "Not a command"), command);
  }
}

static void test_export_replays_to_the_same_settings() {
  char out[300];
  for (size_t i = 0; i < PRESET_COUNT; i++) {
    const Preset& p = PRESETS[i];
    Settings staged{};
    TEST_ASSERT_EQUAL(Action::None, handleCommand("export", p.chip, p.settings, staged, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("export ", out, 7, p.name);
    Settings rebuilt{};
    replay(out + 7, p.chip, rebuilt);
    TEST_ASSERT_TRUE_MESSAGE(rebuilt == p.settings, p.name);
  }

  // A wired-up strip, a serpentine grid, and an RGB LED with no button.
  Settings custom{};
  Settings staged{};
  for (const char* setup : {"pixels 5 30 0; order grb; serpentine off; max 200; button 9; link bluetooth",
                            "pixels 5 64 8; order bgr; serpentine on; max 1; button none; link bluetooth",
                            "rgb 4 5 6; anode on; max 255; button none; link wifi"}) {
    custom = Settings{};
    replay(setup, "esp32s3", custom);
    handleCommand("export", "esp32s3", custom, staged, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(setup, out + 7);
  }

  handleCommand("export", "esp32s3", Settings{}, staged, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("export none", out);
}

static void test_help_fits_and_lists_every_command() {
  Settings s;
  char out[REPLY_SIZE];
  run("help", s, s, out, sizeof(out));
  for (const char* word : {"show", "boards", "board <name>", "pixels", "order", "serpentine", "rgb", "anode", "max",
                           "button", "export", "save", "cancel", "reset", "restart", "pins", "test <pin>", "test off", "about",
                           "screen", "label", "flip", "link", "wifi \"<name>\"", "wifi list", "wifi forget"}) {
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, word), word);
  }
  TEST_ASSERT_LESS_THAN(sizeof(out) - 1, strlen(out));
}

// One firmware for Bluetooth and Wi-Fi lights, LEDs and the T-Display's screen (Hans, 2026-11-09).
static void test_link_screen_label_and_flip_round_trip_and_replay() {
  // Settings saved before these existed read as Bluetooth, with the name shown and not flipped.
  Settings old = findPreset("s3-matrix")->settings;
  uint8_t b[STORED_SIZE];
  encodeSettings(old, b);
  TEST_ASSERT_EQUAL_UINT8(0, b[14]);
  TEST_ASSERT_EQUAL_UINT8(0, b[7] & 0x0c);
  Settings back;
  TEST_ASSERT_TRUE(decodeSettings(b, STORED_SIZE, back));
  TEST_ASSERT_TRUE(back.link == Link::Bluetooth && back.label && !back.flip);

  Settings td = findPreset("tdisplay")->settings;
  TEST_ASSERT_TRUE(td.output == Output::Screen);
  TEST_ASSERT_NULL(problem("esp32", td));
  TEST_ASSERT_NOT_NULL(problem("esp32s3", td));  // the screen is the T-Display's own
  td.link = Link::Wifi;
  td.label = false;
  td.flip = true;
  encodeSettings(td, b);
  TEST_ASSERT_TRUE(decodeSettings(b, STORED_SIZE, back));
  TEST_ASSERT_TRUE(back == td);
  b[14] = 2;
  TEST_ASSERT_FALSE(decodeSettings(b, STORED_SIZE, back));
  b[14] = 1;
  b[1] = 4;
  TEST_ASSERT_FALSE(decodeSettings(b, STORED_SIZE, back));

  // The console sets each one, and export replays to the same settings.
  Settings running{};
  Settings staged = running;
  char out[REPLY_SIZE];
  TEST_ASSERT_EQUAL(Action::None, handleCommand("board tdisplay", "esp32", running, staged, out, sizeof(out)));
  TEST_ASSERT_EQUAL(Action::None, handleCommand("link wifi", "esp32", running, staged, out, sizeof(out)));
  TEST_ASSERT_EQUAL(Action::None, handleCommand("label off", "esp32", running, staged, out, sizeof(out)));
  TEST_ASSERT_EQUAL(Action::None, handleCommand("flip on", "esp32", running, staged, out, sizeof(out)));
  TEST_ASSERT_TRUE(staged.link == Link::Wifi && !staged.label && staged.flip);
  exportCommands(staged, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("screen; label off; flip on; max 200; button 35; link wifi", out);
  Settings replayed{};
  char line[64];
  for (const char* p = out; *p;) {
    const char* end = strstr(p, "; ");
    const size_t n = end != nullptr ? static_cast<size_t>(end - p) : strlen(p);
    snprintf(line, sizeof(line), "%.*s", static_cast<int>(n), p);
    char reply[REPLY_SIZE];
    handleCommand(line, "esp32", running, replayed, reply, sizeof(reply));
    p += n + (end != nullptr ? 2 : 0);
  }
  TEST_ASSERT_TRUE(replayed == staged);
  describe(staged, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "over Wi-Fi"));

  // The ESP32-H2 has no Wi-Fi, and only the ESP32 has the T-Display's screen.
  Settings h2 = findPreset("h2-devkitm")->settings;
  Settings h2staged = h2;
  handleCommand("link wifi", "esp32h2", h2, h2staged, out, sizeof(out));
  TEST_ASSERT_TRUE(h2staged.link == Link::Bluetooth);
  TEST_ASSERT_NOT_NULL(strstr(out, "no Wi-Fi"));
  h2.link = Link::Wifi;
  TEST_ASSERT_NOT_NULL(problem("esp32h2", h2));
  handleCommand("screen", "esp32c3", running, staged, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "only on the LilyGO T-Display"));

  // A screen has its own animations, all of them over the color.
  size_t count = 0;
  const EffectInfo* fx = effectsFor(Output::Screen, count);
  TEST_ASSERT_EQUAL(4, count);
  TEST_ASSERT_TRUE(hasEffect(fx, count, EFFECT_SNOW) && hasEffect(fx, count, EFFECT_LEAVES));
  TEST_ASSERT_FALSE(hasEffect(fx, count, EFFECT_RAINBOW));
  fx = effectsFor(Output::Pixels, count);
  TEST_ASSERT_FALSE(hasEffect(fx, count, EFFECT_SNOW));
}

// Hans, 2026-11-09: a blink to tell several lights apart.
static void test_identify_blinks_six_times_then_stops() {
  int flashes = 0;
  bool last = false;
  for (uint32_t ms = 0; ms < IDENTIFY_MS + 1000; ms += 10) {
    const bool lit = identifyLit(ms);
    if (lit && !last) flashes++;
    last = lit;
  }
  TEST_ASSERT_EQUAL(6, flashes);
  TEST_ASSERT_TRUE(identifyLit(0));
  TEST_ASSERT_FALSE(identifyLit(260));
  TEST_ASSERT_FALSE(identifyLit(IDENTIFY_MS));
  TEST_ASSERT_FALSE(identifyLit(IDENTIFY_MS + 10));
}

// Wi-Fi and Bluetooth both say which board a light is: its preset's description, whatever its link, label, or flip,
// or Custom for wiring set by hand or a preset from another chip.
static void test_board_name_follows_the_wiring_only() {
  for (size_t i = 0; i < PRESET_COUNT; i++) {
    const Preset& p = PRESETS[i];
    TEST_ASSERT_EQUAL_STRING_MESSAGE(p.board, boardName(p.settings, p.chip), p.name);
    Settings chosen = p.settings;
    chosen.link = chosen.link == Link::Wifi ? Link::Bluetooth : Link::Wifi;
    chosen.label = !chosen.label;
    chosen.flip = !chosen.flip;
    TEST_ASSERT_EQUAL_STRING_MESSAGE(p.board, boardName(chosen, p.chip), p.name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Custom", boardName(p.settings, "not-a-chip"), p.name);
    Settings moved = p.settings;
    moved.maxBrightness = static_cast<uint8_t>(moved.maxBrightness == 1 ? 2 : moved.maxBrightness - 1);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Custom", boardName(moved, p.chip), p.name);
  }
  TEST_ASSERT_EQUAL_STRING("Custom", boardName(Settings{}, "esp32s3"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_device_is_not_set_up_and_every_chip_has_presets);
  RUN_TEST(test_every_preset_runs_on_its_chip_and_names_are_unique);
  RUN_TEST(test_pins_for_flash_usb_and_the_console_are_refused);
  RUN_TEST(test_layout_of_a_strip_a_grid_and_a_serpentine_grid);
  RUN_TEST(test_stale_markers_are_grid_corners_and_strip_ends);
  RUN_TEST(test_the_alive_pixel_is_in_the_middle);
  RUN_TEST(test_scene_shows_nothing_off_alive_stale_and_fresh);
  RUN_TEST(test_scene_sends_led_values);
  RUN_TEST(test_brightness_cap_maps_1_to_255_onto_1_to_cap);
  RUN_TEST(test_a_dim_breath_keeps_its_hue_and_fades_smoothly);
  RUN_TEST(test_settings_round_trip_and_bad_data_is_refused);
  RUN_TEST(test_console_stages_changes_until_save);
  RUN_TEST(test_console_refuses_bad_pins_and_values);
  RUN_TEST(test_console_presets_cancel_reset_and_unknown_commands);
  RUN_TEST(test_pins_lists_only_usable_pins_and_not_the_button);
  RUN_TEST(test_test_lights_a_color_and_refuses_bad_requests);
  RUN_TEST(test_a_known_command_with_wrong_arguments_shows_its_usage);
  RUN_TEST(test_export_replays_to_the_same_settings);
  RUN_TEST(test_help_fits_and_lists_every_command);
  RUN_TEST(test_link_screen_label_and_flip_round_trip_and_replay);
  RUN_TEST(test_identify_blinks_six_times_then_stops);
  RUN_TEST(test_board_name_follows_the_wiring_only);
  return UNITY_END();
}

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
  TEST_ASSERT_TRUE(staged == fresh);  // a test never stages a change

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
  for (const char* setup : {"pixels 5 30 0; order grb; serpentine off; max 200; button 9",
                            "pixels 5 64 8; order bgr; serpentine on; max 1; button none",
                            "rgb 4 5 6; anode on; max 255; button none"}) {
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
  char out[1200];
  run("help", s, s, out, sizeof(out));
  for (const char* word : {"show", "boards", "board <name>", "pixels", "order", "serpentine", "rgb", "anode", "max",
                           "button", "export", "save", "cancel", "reset", "restart", "pins", "test <pin>", "test off", "about"}) {
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, word), word);
  }
  TEST_ASSERT_LESS_THAN(sizeof(out) - 1, strlen(out));
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
  RUN_TEST(test_brightness_cap_maps_1_to_255_onto_1_to_cap);
  RUN_TEST(test_settings_round_trip_and_bad_data_is_refused);
  RUN_TEST(test_console_stages_changes_until_save);
  RUN_TEST(test_console_refuses_bad_pins_and_values);
  RUN_TEST(test_console_presets_cancel_reset_and_unknown_commands);
  RUN_TEST(test_pins_lists_only_usable_pins_and_not_the_button);
  RUN_TEST(test_test_lights_a_color_and_refuses_bad_requests);
  RUN_TEST(test_a_known_command_with_wrong_arguments_shows_its_usage);
  RUN_TEST(test_export_replays_to_the_same_settings);
  RUN_TEST(test_help_fits_and_lists_every_command);
  return UNITY_END();
}

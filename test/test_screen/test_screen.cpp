#include <color.h>
#include <screen.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "vectors.h"

using namespace cheerlights;

void setUp() {}
void tearDown() {}

static uint16_t buffer[240 * 135];

// ---- Colors ----

static void test_palette_names_and_ink_match_colors_json() {
  TEST_ASSERT_EQUAL(PALETTE_VECTOR_COUNT, PALETTE_SIZE);
  for (const PaletteVector& v : PALETTE_VECTORS) {
    Rgb parsed{};
    TEST_ASSERT_TRUE_MESSAGE(parseHex(v.hex, strlen(v.hex), parsed), v.name);
    TEST_ASSERT_TRUE_MESSAGE(parsed == (Rgb{v.r, v.g, v.b}), v.name);
    TEST_ASSERT_EQUAL_STRING(v.name, PALETTE[v.index].name);
    TEST_ASSERT_EQUAL_STRING(v.name, paletteName(parsed));
    TEST_ASSERT_EQUAL_MESSAGE(v.whiteText, whiteInkFor(parsed), v.name);
  }
  // Mid gray, where the two contrasts cross: 117 still reads better in white, 118 in black.
  TEST_ASSERT_TRUE(whiteInkFor({117, 117, 117}));
  TEST_ASSERT_FALSE(whiteInkFor({118, 118, 118}));
}

static void test_mqtt_payloads_parse_and_label() {
  Rgb c{};
  const char* ok[] = {"#800080", "800080", " #800080\n", "#FF00ff"};
  for (const char* s : ok) TEST_ASSERT_TRUE_MESSAGE(parseHex(s, strlen(s), c), s);
  TEST_ASSERT_TRUE(c == (Rgb{255, 0, 255}));
  for (const char* s : {"", "#80008", "#8000800", "purple", "#80g080", "##800080"}) {
    TEST_ASSERT_FALSE_MESSAGE(parseHex(s, strlen(s), c), s);
  }
  char label[8];
  colorLabel({0x80, 0x00, 0x80}, label);
  TEST_ASSERT_EQUAL_STRING("purple", label);
  colorLabel({0x12, 0xab, 0xef}, label);
  TEST_ASSERT_EQUAL_STRING("#12abef", label);
  TEST_ASSERT_NULL(paletteName({0x80, 0x00, 0x81}));
}

// White snow and twinkles wherever white shows; a gray on the five light colors.
static void test_accent_is_white_except_on_light_colors() {
  for (size_t i = 0; i < PALETTE_SIZE; i++) {
    const char* n = PALETTE[i].name;
    const bool light = strcmp(n, "white") == 0 || strcmp(n, "oldlace") == 0 || strcmp(n, "yellow") == 0 ||
                       strcmp(n, "cyan") == 0 || strcmp(n, "pink") == 0;
    const Rgb a = accentFor(PALETTE[i].rgb);
    TEST_ASSERT_EQUAL_MESSAGE(!light, a == (Rgb{255, 255, 255}), n);
    if (light) TEST_ASSERT_TRUE_MESSAGE(luminance(a) < luminance(PALETTE[i].rgb) - 0.2, n);
  }
}

// ---- Drawing ----

static void test_rgb565_keeps_black_white_and_the_palette_close() {
  TEST_ASSERT_TRUE(from565(to565({0, 0, 0})) == (Rgb{0, 0, 0}));
  TEST_ASSERT_TRUE(from565(to565({255, 255, 255})) == (Rgb{255, 255, 255}));
  TEST_ASSERT_EQUAL_HEX16(0xF800, to565({255, 0, 0}));
  for (size_t i = 0; i < PALETTE_SIZE; i++) {
    const Rgb a = PALETTE[i].rgb, b = from565(to565(a));
    TEST_ASSERT_INT_WITHIN_MESSAGE(8, a.r, b.r, PALETTE[i].name);
    TEST_ASSERT_INT_WITHIN_MESSAGE(4, a.g, b.g, PALETTE[i].name);
    TEST_ASSERT_INT_WITHIN_MESSAGE(8, a.b, b.b, PALETTE[i].name);
  }
}

static void test_shapes_cover_their_middle_and_stay_in_bounds() {
  Canvas c(buffer, 240, 135);
  c.fill({0, 0, 0});
  c.disc(100, 60, 5, {255, 255, 255});
  TEST_ASSERT_TRUE(c.get(100, 60) == (Rgb{255, 255, 255}));
  TEST_ASSERT_TRUE(c.get(110, 60) == (Rgb{0, 0, 0}));
  // Partly or wholly off the frame: drawn where it shows, and nothing written outside the buffer.
  uint16_t guarded[240 * 135 + 64];
  for (auto& p : guarded) p = 0x1234;
  Canvas g(guarded, 240, 135);
  g.disc(-3, -3, 6, {255, 0, 0});
  g.disc(239, 134, 6, {255, 0, 0});
  g.leaf(240, 135, 30, 1, 1, {255, 0, 0}, {0, 0, 0});
  g.twinkle(0, 134, 9, {255, 255, 255}, 1);
  g.text(FONT_NAME, 200, 140, "magenta", {255, 255, 255});
  for (int i = 240 * 135; i < 240 * 135 + 64; i++) TEST_ASSERT_EQUAL_HEX16(0x1234, guarded[i]);
  TEST_ASSERT_TRUE(g.get(0, 0) == (Rgb{255, 0, 0}));
}

static int countNot(const Canvas& c, Rgb background, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) n += c.get(x, y) != background ? 1 : 0;
  }
  return n;
}

static void test_text_draws_inside_its_width_and_fit_text_cuts_long_names() {
  Canvas c(buffer, 240, 135);
  c.fill({0, 0, 0});
  const int w = textWidth(FONT_NAME, "magenta");
  TEST_ASSERT_GREATER_THAN(100, w);
  TEST_ASSERT_LESS_THAN(240 - 28, w);  // the longest palette name fits between the margins
  c.text(FONT_NAME, 14, 115, "magenta", {255, 255, 255});
  TEST_ASSERT_GREATER_THAN(500, countNot(c, {0, 0, 0}, 0, 0, 240, 135));
  TEST_ASSERT_EQUAL(0, countNot(c, {0, 0, 0}, 14 + w + 2, 0, 240, 135));
  TEST_ASSERT_EQUAL(0, countNot(c, {0, 0, 0}, 0, 0, 240, 115 - FONT_NAME.ascent));
  for (size_t i = 0; i < PALETTE_SIZE; i++) TEST_ASSERT_LESS_THAN(212, textWidth(FONT_NAME, PALETTE[i].name));
  TEST_ASSERT_LESS_THAN(212, textWidth(FONT_NAME, "#bbbbbb"));

  char out[64];
  fitText(FONT_TEXT, "A network name that is far too long for the screen", 120, out, sizeof(out));
  TEST_ASSERT_LESS_OR_EQUAL(120, textWidth(FONT_TEXT, out));
  TEST_ASSERT_NOT_NULL(strstr(out, "..."));
  fitText(FONT_TEXT, "Home", 120, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Home", out);
}

// ---- Animations ----

static void test_scenes_cycle_by_name_and_repeat_from_a_seed() {
  SceneId id = SceneId::Solid;
  const char* order[] = {"snow", "leaves", "sparkle", "solid"};
  for (const char* name : order) {
    id = nextScene(id);
    TEST_ASSERT_EQUAL_STRING(name, sceneName(id));
    SceneId back;
    TEST_ASSERT_TRUE(sceneFromName(name, back));
    TEST_ASSERT_TRUE(back == id);
  }
  SceneId none;
  TEST_ASSERT_FALSE(sceneFromName("rain", none));

  static SceneState a, b;
  sceneStart(a, SceneId::Snow, 240, 135, 42);
  sceneStart(b, SceneId::Snow, 240, 135, 42);
  for (int i = 0; i < 300; i++) {
    sceneStep(a, 1 / 30.0f);
    sceneStep(b, 1 / 30.0f);
  }
  TEST_ASSERT_EQUAL_MEMORY(a.p, b.p, sizeof(a.p));
  sceneStart(a, SceneId::Solid, 240, 135, 42);
  TEST_ASSERT_EQUAL(0, a.count);
  TEST_ASSERT_FALSE(sceneMoves(SceneId::Solid));
  TEST_ASSERT_TRUE(sceneMoves(SceneId::Leaves));
}

// After ten minutes, every animation still has particles on the screen and none has wandered off for good.
static void test_animations_keep_going_and_stay_near_the_screen() {
  static SceneState s;
  for (SceneId id : {SceneId::Snow, SceneId::Leaves, SceneId::Sparkle}) {
    sceneStart(s, id, 240, 135, 7);
    for (int i = 0; i < 30 * 600; i++) sceneStep(s, 1 / 30.0f);
    int onScreen = 0;
    for (int i = 0; i < s.count; i++) {
      const Particle& p = s.p[i];
      TEST_ASSERT_TRUE_MESSAGE(p.y > -135 - 40 && p.y < 135 + 40, sceneName(id));
      TEST_ASSERT_TRUE_MESSAGE(p.x > -100 && p.x < 240 + 40, sceneName(id));
      onScreen += (p.x >= 0 && p.x < 240 && p.y >= 0 && p.y < 135) ? 1 : 0;
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(s.count / 4, onScreen, sceneName(id));
  }
}

// ---- Screens ----

static View showing(Rgb color) {
  View v{};
  v.status = Status::Showing;
  v.color = v.from = color;
  v.fade = 1;
  v.setupUrl = "cheerlights.com/tdisplay";
  v.ssid = "Home";
  return v;
}

static void test_the_color_fills_the_screen_and_its_name_is_written_on_it() {
  Canvas c(buffer, 240, 135);
  static SceneState scene;
  sceneStart(scene, SceneId::Solid, 240, 135, 1);
  const Rgb purple{0x80, 0x00, 0x80};
  drawView(c, showing(purple), scene);
  TEST_ASSERT_TRUE(c.get(0, 0) == from565(to565(purple)));
  TEST_ASSERT_TRUE(c.get(239, 0) == from565(to565(purple)));
  TEST_ASSERT_GREATER_THAN(500, countNot(c, from565(to565(purple)), 0, 60, 240, 135));  // the name, in white
  TEST_ASSERT_EQUAL(0, countNot(c, from565(to565(purple)), 0, 0, 240, 60));  // nothing above the name on solid
  View hidden = showing(purple);
  hidden.hideLabel = true;
  drawView(c, hidden, scene);
  TEST_ASSERT_EQUAL(0, countNot(c, from565(to565(purple)), 0, 0, 240, 135));  // the owner turned the name off

  // Halfway through a change from green, the background is between the two.
  View v = showing(purple);
  v.from = {0x00, 0x80, 0x00};
  v.fade = 0.5f;
  drawView(c, v, scene);
  const Rgb mid = c.get(239, 0);
  TEST_ASSERT_INT_WITHIN(12, 0x40, mid.r);
  TEST_ASSERT_INT_WITHIN(12, 0x40, mid.g);
}

static void test_setup_screens_name_the_page_and_the_network() {
  Canvas c(buffer, 240, 135);
  static SceneState scene;
  sceneStart(scene, SceneId::Snow, 240, 135, 1);
  for (Status st : {Status::NoWifi, Status::Scanning, Status::Joining, Status::JoinFailed, Status::NoneInRange,
                    Status::Connecting, Status::Waiting}) {
    View v = showing({255, 0, 0});
    v.status = st;
    drawView(c, v, scene);
    const Rgb ground = c.get(0, 0);
    TEST_ASSERT_TRUE(ground != from565(to565({255, 0, 0})));  // the dark ground, not the color
    TEST_ASSERT_GREATER_THAN(300, countNot(c, ground, 0, 0, 240, 135));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_palette_names_and_ink_match_colors_json);
  RUN_TEST(test_mqtt_payloads_parse_and_label);
  RUN_TEST(test_accent_is_white_except_on_light_colors);
  RUN_TEST(test_rgb565_keeps_black_white_and_the_palette_close);
  RUN_TEST(test_shapes_cover_their_middle_and_stay_in_bounds);
  RUN_TEST(test_text_draws_inside_its_width_and_fit_text_cuts_long_names);
  RUN_TEST(test_scenes_cycle_by_name_and_repeat_from_a_seed);
  RUN_TEST(test_animations_keep_going_and_stay_near_the_screen);
  RUN_TEST(test_the_color_fills_the_screen_and_its_name_is_written_on_it);
  RUN_TEST(test_setup_screens_name_the_page_and_the_network);
  return UNITY_END();
}

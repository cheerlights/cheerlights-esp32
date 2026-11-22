#include <device.h>
#include <stdio.h>
#include <string.h>
#include <text.h>
#include <unity.h>

#include "../test_profile/vectors.h"

using namespace cheerlights;

void setUp() {}
void tearDown() {}

// ---- The words ----

static void test_phone_keyboard_characters_become_ones_the_font_has() {
  char out[TEXT_MAX + 1];
  TEST_ASSERT_NULL(normalizeText("  Don\xe2\x80\x99t stop \xe2\x80\x9c" "believing\xe2\x80\x9d \xe2\x80\x94 ok\xe2\x80\xa6  ", out));
  TEST_ASSERT_EQUAL_STRING("Don't stop \"believing\" - ok...", out);
  TEST_ASSERT_NULL(normalizeText("Cr\xc3\xa8me br\xc3\xbbl\xc3\xa9" "e, Stra\xc3\x9f" "e, \xc3\x86on", out));
  TEST_ASSERT_EQUAL_STRING("Creme brulee, Strasse, AEon", out);
  TEST_ASSERT_NULL(normalizeText("tab\there\nand\xc2\xa0there", out));
  TEST_ASSERT_EQUAL_STRING("tab here and there", out);
}

static void test_characters_with_no_plain_form_and_long_text_are_refused() {
  char out[TEXT_MAX + 1] = "kept";
  TEST_ASSERT_NOT_NULL(normalizeText("snow \xe2\x98\x83", out));          // a snowman
  TEST_ASSERT_NOT_NULL(normalizeText("\xf0\x9f\x8e\x84 tree", out));      // an emoji
  TEST_ASSERT_NOT_NULL(normalizeText("broken \xe2\x80", out));            // cut off in the middle of a character
  TEST_ASSERT_NOT_NULL(normalizeText("\xc3\xb7", out));                   // a division sign
  char longest[TEXT_MAX + 2];
  memset(longest, 'a', TEXT_MAX);
  longest[TEXT_MAX] = '\0';
  TEST_ASSERT_NULL(normalizeText(longest, out));
  TEST_ASSERT_EQUAL(TEXT_MAX, strlen(out));
  longest[TEXT_MAX] = 'a';
  longest[TEXT_MAX + 1] = '\0';
  TEST_ASSERT_EQUAL_STRING("The text is longer than 64 characters.", normalizeText(longest, out));
  // An ellipsis is three characters, so it counts as three.
  char nearly[TEXT_MAX * 2];
  memset(nearly, 'a', TEXT_MAX - 2);
  strcpy(nearly + TEXT_MAX - 2, "\xe2\x80\xa6");
  TEST_ASSERT_NOT_NULL(normalizeText(nearly, out));
}

// ---- The layout ----

static void test_letters_are_as_wide_as_their_ink_one_column_apart() {
  TextStrip strip;
  layoutText("H", strip);
  TEST_ASSERT_EQUAL(5, strip.width);
  layoutText("Hi", strip);
  TEST_ASSERT_EQUAL(5 + 1 + 3, strip.width);  // the i is three columns
  layoutText("i i", strip);
  TEST_ASSERT_EQUAL(3 + 1 + 2 + 1 + 3, strip.width);  // a space is two blank columns plus the gap
  layoutText("", strip);
  TEST_ASSERT_EQUAL(0, strip.width);
  // The tail of a g is in the bottom row, the eighth.
  layoutText("g", strip);
  int bottom = 0;
  for (int i = 0; i < strip.width; i++) bottom |= strip.columns[i] & 0x80;
  TEST_ASSERT_TRUE(bottom != 0);
  // The longest message of the widest letters fits.
  char wide[TEXT_MAX + 1];
  memset(wide, 'M', TEXT_MAX);
  wide[TEXT_MAX] = '\0';
  layoutText(wide, strip);
  TEST_ASSERT_EQUAL(TEXT_MAX * 6 - 1, strip.width);
}

// The message comes in at the right edge, goes out at the left, and a grid's width of plain color follows.
static void test_text_scrolls_in_from_the_right_and_leaves_a_gap_of_plain_color() {
  TextStrip strip;
  layoutText("I", strip);  // three columns, the middle one lit top to bottom of the letter
  const uint8_t speed = 255;
  const uint32_t cps = textColumnsPerSecond(speed);
  TEST_ASSERT_EQUAL(24, cps);
  TEST_ASSERT_EQUAL(4, textColumnsPerSecond(1));
  auto litColumn = [&](uint32_t step) {
    const uint32_t ms = step * 1000 / cps + 1;
    int found = -1;
    for (int x = 0; x < 8; x++) {
      if (textLit(strip, speed, ms, x, 3, 8, 8)) found = x;
    }
    return found;
  };
  TEST_ASSERT_EQUAL(-1, litColumn(0));  // still off the right edge
  TEST_ASSERT_EQUAL(-1, litColumn(1));
  TEST_ASSERT_EQUAL(7, litColumn(2));   // the middle column arrives at the right edge
  TEST_ASSERT_EQUAL(0, litColumn(9));   // and reaches the left edge seven steps later
  TEST_ASSERT_EQUAL(-1, litColumn(10));
  // Plain color until the cycle, 8 + 3 + 8 columns, starts again.
  for (uint32_t step = 10; step < 19; step++) TEST_ASSERT_EQUAL(-1, litColumn(step));
  TEST_ASSERT_EQUAL(7, litColumn(19 + 2));
  // Rows outside the letter stay dark.
  TEST_ASSERT_FALSE(textLit(strip, speed, 9 * 1000 / cps + 1, 0, 7, 8, 8));
  // A grid shorter than the font shows nothing, and a grid of 16 rows shows the letters twice as big.
  TEST_ASSERT_FALSE(textLit(strip, speed, 9 * 1000 / cps + 1, 0, 3, 8, 7));
  layoutText("I", strip);
  int lit = 0;
  for (uint32_t ms = 0; ms < 4000; ms += 5) {
    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 16; x++) lit += textLit(strip, speed, ms, x, y, 16, 16) ? 1 : 0;
    }
  }
  TEST_ASSERT_TRUE(lit > 0);
  TEST_ASSERT_FALSE(textLit(strip, speed, 2000, 0, 0, 16, 16) && textLit(strip, speed, 2000, 0, 15, 16, 16));
}

// A turn moves the tops of the letters to another edge, so a grid mounted any way round reads upright. The bar of a T
// is its top row: at no turn it lies along the top edge, a quarter turn puts it along the right edge, and so on.
static void test_a_turn_puts_the_tops_of_the_letters_on_another_edge() {
  TextStrip strip;
  layoutText("T", strip);
  const uint8_t speed = 255;
  // When the T sits in the middle of the grid: it arrives after 2 columns and is 5 wide, so 2 + 5 steps.
  const uint32_t ms = 7 * 1000 / textColumnsPerSecond(speed) + 1;
  for (uint8_t turn = 0; turn < 4; turn++) {
    int rows[8] = {}, columns[8] = {};
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (textLit(strip, speed, ms, x, y, 8, 8, turn)) {
          rows[y]++;
          columns[x]++;
        }
      }
    }
    char message[24];
    snprintf(message, sizeof(message), "turn %u", turn);
    // The bar is five pixels in a line along one edge.
    if (turn == 0) TEST_ASSERT_EQUAL_MESSAGE(5, rows[0], message);
    if (turn == 1) TEST_ASSERT_EQUAL_MESSAGE(5, columns[7], message);
    if (turn == 2) TEST_ASSERT_EQUAL_MESSAGE(5, rows[7], message);
    if (turn == 3) TEST_ASSERT_EQUAL_MESSAGE(5, columns[0], message);
  }
  // Text that is kept remembers its turn; text saved before turns existed reads as no turn.
  ScrollText t;
  strcpy(t.message, "Hi");
  t.turn = 3;
  uint8_t stored[TEXT_STORED_SIZE];
  encodeText(t, stored);
  ScrollText back;
  TEST_ASSERT_TRUE(decodeText(stored, sizeof(stored), back));
  TEST_ASSERT_EQUAL(3, back.turn);
  stored[1] &= 3;
  TEST_ASSERT_TRUE(decodeText(stored, sizeof(stored), back));
  TEST_ASSERT_EQUAL(0, back.turn);
  // The console takes degrees.
  char out[200];
  TEST_ASSERT_EQUAL(TextAction::Changed, handleTextCommand("text turn 180", true, t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(2, t.turn);
  TEST_ASSERT_EQUAL(TextAction::None, handleTextCommand("text turn 45", true, t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(TextAction::None, handleTextCommand("text turn -90", true, t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(2, t.turn);
  handleTextCommand("text", true, t, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("text off \"Hi\" color auto speed 128 turn 180", out);
}

// ---- The letters' color ----

static void test_auto_draws_dark_letters_on_light_colors_and_white_on_dark_ones() {
  const ScrollText automatic;
  const Rgb8 white{0xff, 0xff, 0xff}, dark{0, 0, 0};
  const char* const LIGHT[] = {"green", "cyan", "white", "oldlace", "yellow"};
  const char* const DARK[] = {"red", "blue", "purple", "magenta", "orange", "pink"};
  for (size_t i = 0; i < PALETTE_SIZE; i++) {
    bool light = false;
    for (const char* name : LIGHT) light = light || strcmp(PALETTE[i].name, name) == 0;
    bool darkColor = false;
    for (const char* name : DARK) darkColor = darkColor || strcmp(PALETTE[i].name, name) == 0;
    TEST_ASSERT_TRUE_MESSAGE(light != darkColor, PALETTE[i].name);
    TEST_ASSERT_TRUE_MESSAGE(textInk(automatic, LED_PALETTE[i], false) == (light ? dark : white), PALETTE[i].name);
  }
  // An animation with its own colors, such as the rainbow, gets dark letters, which read on any of them.
  TEST_ASSERT_TRUE(textInk(automatic, LED_PALETTE[2], true) == dark);
  // A color the owner picks is used as the LEDs show it, through the LED palette.
  ScrollText chosen;
  chosen.autoColor = false;
  chosen.color = {0xff, 0xc0, 0xcb};  // pink as published
  TEST_ASSERT_TRUE(textInk(chosen, white, false) == (Rgb8{0xff, 0x24, 0x54}));
  chosen.color = dark;
  TEST_ASSERT_TRUE(textInk(chosen, LED_PALETTE[2], false) == dark);
}

// ---- Kept across restarts ----

static void test_text_survives_a_restart_and_bad_bytes_are_refused() {
  ScrollText t;
  strcpy(t.message, "Happy holidays!");
  t.on = true;
  t.autoColor = false;
  t.color = {1, 2, 3};
  t.speed = 9;
  uint8_t stored[TEXT_STORED_SIZE];
  encodeText(t, stored);
  ScrollText back;
  TEST_ASSERT_TRUE(decodeText(stored, sizeof(stored), back));
  TEST_ASSERT_TRUE(back == t);
  ScrollText untouched;
  stored[7] = 0x07;  // not printable
  TEST_ASSERT_FALSE(decodeText(stored, sizeof(stored), untouched));
  encodeText(t, stored);
  stored[0] = 9;  // a version this firmware does not know
  TEST_ASSERT_FALSE(decodeText(stored, sizeof(stored), untouched));
  encodeText(t, stored);
  stored[6] = TEXT_MAX + 1;
  TEST_ASSERT_FALSE(decodeText(stored, sizeof(stored), untouched));
  TEST_ASSERT_FALSE(decodeText(stored, sizeof(stored) - 1, untouched));
  TEST_ASSERT_TRUE(untouched == ScrollText{});
}

// ---- The console ----

static TextAction run(const char* line, ScrollText& t, char* out, size_t size, bool scrolls = true) {
  return handleTextCommand(line, scrolls, t, out, size);
}

static void test_the_console_sets_shows_and_clears_the_text() {
  char out[400];
  ScrollText t;
  TEST_ASSERT_EQUAL(TextAction::NotText, run("textile", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(TextAction::NotText, run("wifi list", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(TextAction::None, run("text on", t, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(strstr(out, "no text yet"));

  TEST_ASSERT_EQUAL(TextAction::Changed, run("TEXT \"Hello, world\"", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Hello, world", t.message);
  TEST_ASSERT_TRUE(t.on);
  // Words without quotes work too, and quotes inside quotes are kept.
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text Merry and bright", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Merry and bright", t.message);
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text \"on\" the \"air\"", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("on\" the \"air", t.message);
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text \"on\"", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("on", t.message);  // in quotes, on is a word, not the command

  TEST_ASSERT_EQUAL(TextAction::Changed, run("text off", t, out, sizeof(out)));
  TEST_ASSERT_FALSE(t.on);
  TEST_ASSERT_EQUAL_STRING("on", t.message);
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text on", t, out, sizeof(out)));
  TEST_ASSERT_TRUE(t.on);
  TEST_ASSERT_EQUAL(TextAction::None, run("text on", t, out, sizeof(out)));  // already on

  TEST_ASSERT_EQUAL(TextAction::Changed, run("text color dark", t, out, sizeof(out)));
  TEST_ASSERT_FALSE(t.autoColor);
  TEST_ASSERT_TRUE(t.color == (Rgb8{0, 0, 0}));
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text color #FF8800", t, out, sizeof(out)));
  TEST_ASSERT_TRUE(t.color == (Rgb8{0xff, 0x88, 0x00}));
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text color auto", t, out, sizeof(out)));
  TEST_ASSERT_TRUE(t.autoColor);
  TEST_ASSERT_EQUAL(TextAction::None, run("text color teal", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text speed 40", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(40, t.speed);
  TEST_ASSERT_EQUAL(TextAction::None, run("text speed 0", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(TextAction::None, run("text speed fast", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(40, t.speed);

  TEST_ASSERT_EQUAL(TextAction::None, run("text", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("text on \"on\" color auto speed 40 turn 0", out);
  TEST_ASSERT_EQUAL(TextAction::None, run("text \"snow \xe2\x98\x83\"", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("on", t.message);
  TEST_ASSERT_EQUAL(TextAction::None, run("text \"   \"", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL(TextAction::Changed, run("text clear", t, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", t.message);
  TEST_ASSERT_FALSE(t.on);

  // A light that cannot scroll says so and changes nothing.
  const ScrollText before = t;
  TEST_ASSERT_EQUAL(TextAction::None, run("text \"Hi\"", t, out, sizeof(out), false));
  TEST_ASSERT_NOT_NULL(strstr(out, "cannot show text"));
  TEST_ASSERT_TRUE(t == before);
}

// ---- On the LEDs ----

static void test_text_scrolls_over_the_color_only_on_a_grid_that_can_show_it() {
  const Settings matrix = findPreset("s3-matrix")->settings;
  TEST_ASSERT_TRUE(canScroll(matrix));
  TEST_ASSERT_FALSE(canScroll(findPreset("atom-matrix")->settings));  // five by five
  TEST_ASSERT_FALSE(canScroll(findPreset("s3-zero")->settings));      // one LED
  TEST_ASSERT_FALSE(canScroll(findPreset("tdisplay")->settings));

  TextShow show;
  strcpy(show.text.message, "HELLO");
  show.text.on = true;
  layoutText(show.text.message, show.strip);
  Scene scene{};
  scene.haveColor = true;
  scene.frame.paletteIndex = 2;  // blue, a dark color: white letters
  scene.frame.b = 0xff;
  scene.text = &show;
  TEST_ASSERT_TRUE(textShowing(matrix, scene));
  TEST_ASSERT_TRUE(sceneMoves(matrix, scene));
  TEST_ASSERT_FALSE(sceneMoves(scene));  // solid blue alone does not move

  // Over a whole pass, every pixel is either the color or a white letter, and some are letters.
  const Rgb8 blue = LED_PALETTE[2], white{0xff, 0xff, 0xff};
  int letters = 0;
  for (uint32_t ms = 0; ms < 3000; ms += 20) {
    scene.ms = ms;
    for (int i = 0; i < 64; i++) {
      const Rgb8 p = scenePixel(matrix, scene, i);
      TEST_ASSERT_TRUE(p == blue || p == white);
      letters += p == white ? 1 : 0;
    }
  }
  TEST_ASSERT_TRUE(letters > 0);

  // Off, without words, before a color, and on a light that cannot show it: no text.
  show.text.on = false;
  TEST_ASSERT_FALSE(textShowing(matrix, scene));
  show.text.on = true;
  scene.off = true;
  TEST_ASSERT_FALSE(textShowing(matrix, scene));
  scene.off = false;
  scene.haveColor = false;
  TEST_ASSERT_FALSE(textShowing(matrix, scene));
  scene.haveColor = true;
  TEST_ASSERT_FALSE(textShowing(findPreset("s3-zero")->settings, scene));
  scene.text = nullptr;
  TEST_ASSERT_FALSE(textShowing(matrix, scene));
}

static void test_a_stale_color_keeps_its_text_and_picks_letters_for_the_dimmed_color() {
  const Settings matrix = findPreset("s3-matrix")->settings;
  TextShow show;
  strcpy(show.text.message, "MMMMMMMMMM");
  show.text.on = true;
  layoutText(show.text.message, show.strip);
  Scene scene{};
  scene.haveColor = true;
  scene.frame.paletteIndex = 4;  // white
  scene.frame.r = scene.frame.g = scene.frame.b = 0xff;
  scene.text = &show;
  scene.ms = 1000;  // the M's fill the grid by now
  bool darkLetter = false;
  for (int i = 0; i < 64; i++) darkLetter = darkLetter || scenePixel(matrix, scene, i) == Rgb8{0, 0, 0};
  TEST_ASSERT_TRUE(darkLetter);  // white is light: dark letters
  scene.frame.stale = true;      // dimmed to a third, white is no longer light: white letters
  bool whiteLetter = false;
  for (int i = 0; i < 64; i++) whiteLetter = whiteLetter || scenePixel(matrix, scene, i) == Rgb8{0xff, 0xff, 0xff};
  TEST_ASSERT_TRUE(whiteLetter);
  TEST_ASSERT_TRUE(sceneMoves(matrix, scene));  // a stale color has no animation, but its text still scrolls
}

// Flow is offered only where a motion sensor answered; its liquid shows the color, dry pixels are dark, and an Auto
// letter on a dry pixel lights in the color, so the words read over the empty part too.
static void test_flow_shows_the_liquid_in_the_color_and_text_reads_over_it() {
  size_t count = 0;
  const EffectInfo* fx = effectsFor(Output::Pixels, false, count);
  TEST_ASSERT_FALSE(hasEffect(fx, count, EFFECT_FLOW));
  fx = effectsFor(Output::Pixels, true, count);
  TEST_ASSERT_TRUE(hasEffect(fx, count, EFFECT_FLOW));
  TEST_ASSERT_EQUAL(EFFECT_SHOWS_COLOR, effectFlags(fx, count, EFFECT_FLOW));  // so it plays in CheerLights mode
  fx = effectsFor(Output::Screen, true, count);
  TEST_ASSERT_FALSE(hasEffect(fx, count, EFFECT_FLOW));
  TEST_ASSERT_TRUE(effectMoves(EFFECT_FLOW));
  int sda = 0, scl = 0;
  TEST_ASSERT_TRUE(motionPins(findPreset("s3-matrix")->settings, "esp32s3", sda, scl));
  TEST_ASSERT_EQUAL(11, sda);
  TEST_ASSERT_EQUAL(12, scl);
  Settings custom = findPreset("s3-matrix")->settings;
  custom.pixelPin = 13;  // wired by hand: no pin is driven for a sensor
  TEST_ASSERT_FALSE(motionPins(custom, "esp32s3", sda, scl));
  TEST_ASSERT_FALSE(motionPins(findPreset("s3-zero")->settings, "esp32s3", sda, scl));

  const Settings matrix = findPreset("s3-matrix")->settings;
  Flow liquid;
  liquid.reset(8, 8);  // the bottom half, at rest
  Scene scene{};
  scene.haveColor = true;
  scene.frame.paletteIndex = 1;  // green, a light color
  scene.frame.g = 0x80;
  scene.light.effect = EFFECT_FLOW;
  scene.flow = &liquid;
  const Rgb8 green = LED_PALETTE[1], dark{0, 0, 0};
  TEST_ASSERT_TRUE(scenePixel(matrix, scene, 7 * 8 + 3) == green);  // bottom row: liquid
  TEST_ASSERT_TRUE(scenePixel(matrix, scene, 0 * 8 + 3) == dark);   // top row: dry
  TextShow show;
  strcpy(show.text.message, "MMMMMMMMMM");
  show.text.on = true;
  layoutText(show.text.message, show.strip);
  scene.text = &show;
  scene.ms = 1000;  // the M's fill the grid
  bool litOnDry = false, darkOnLiquid = false;
  for (int i = 0; i < 64; i++) {
    const Rgb8 p = scenePixel(matrix, scene, i);
    if (i < 32 && p == green) litOnDry = true;      // top half, dry: a letter lit in the color
    if (i >= 32 && p == dark) darkOnLiquid = true;  // bottom half, liquid: a dark letter
  }
  TEST_ASSERT_TRUE(litOnDry);
  TEST_ASSERT_TRUE(darkOnLiquid);
}

// The Text characteristic (docs/ble-profile.md), against the published vectors.
static void test_text_values_encode_as_published() {
  for (const TextValueVector& v : TEXT_VALUES) {
    ScrollText text;
    snprintf(text.message, sizeof(text.message), "%s", v.message);
    text.on = v.on;
    text.autoColor = v.autoColor;
    text.color = {v.r, v.g, v.b};
    text.speed = v.speed;
    text.turn = v.turn;
    uint8_t out[TEXT_VALUE_SIZE];
    TEST_ASSERT_EQUAL_MESSAGE(v.length, encodeTextValue(text, static_cast<TextRefusal>(v.refusal), out), v.name);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v.bytes, out, v.length, v.name);
  }
}

static void test_a_phone_write_is_kept_in_the_font_or_refused_with_a_reason() {
  ScrollText current;
  snprintf(current.message, sizeof(current.message), "Before");
  current.on = true;
  current.speed = 40;
  for (const TextWriteVector& v : TEXT_WRITES) {
    ScrollText out;
    TextRefusal refusal = TextRefusal::None;
    TEST_ASSERT_TRUE_MESSAGE(parseTextValue(v.bytes, v.length, current, out, refusal) == ParseResult::Ok, v.name);
    TEST_ASSERT_EQUAL_MESSAGE(v.refusal, static_cast<uint8_t>(refusal), v.name);
    if (v.kept == nullptr) {
      // Turned down: the light keeps what it had.
      TEST_ASSERT_TRUE_MESSAGE(out == current, v.name);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v.kept, out.message, v.name);
    TEST_ASSERT_EQUAL_MESSAGE(v.on, out.on, v.name);
    TEST_ASSERT_EQUAL_MESSAGE(v.autoColor, out.autoColor, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.speed, out.speed, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.turn, out.turn, v.name);
    TEST_ASSERT_TRUE_MESSAGE((out.color == Rgb8{v.r, v.g, v.b}), v.name);
  }
}

static void test_bad_text_values_are_ignored_and_longer_ones_read() {
  ScrollText current, out;
  TextRefusal refusal;
  for (const TextRejectedVector& v : TEXT_REJECTED) {
    TEST_ASSERT_TRUE_MESSAGE(parseTextValue(v.bytes, v.length, current, out, refusal) != ParseResult::Ok, v.name);
  }
  TEST_ASSERT_TRUE(parseTextValue(nullptr, 8, current, out, refusal) != ParseResult::Ok);
  // A value from a later phone carries more after the message; this light reads the fields it knows.
  TEST_ASSERT_TRUE(parseTextValue(TEXT_NEWER, TEXT_NEWER_LENGTH, current, out, refusal) == ParseResult::Ok);
  TEST_ASSERT_EQUAL_STRING("Hello", out.message);
  TEST_ASSERT_TRUE(out.on);
}

// What a light keeps, encoded and written back, is kept the same: a phone that resends a read changes nothing.
static void test_a_read_written_back_changes_nothing() {
  for (const TextValueVector& v : TEXT_VALUES) {
    ScrollText current, out;
    TextRefusal refusal;
    TEST_ASSERT_TRUE_MESSAGE(parseTextValue(v.bytes, v.length, current, out, refusal) == ParseResult::Ok, v.name);
    uint8_t again[TEXT_VALUE_SIZE];
    const size_t n = encodeTextValue(out, static_cast<TextRefusal>(v.refusal), again);
    TEST_ASSERT_EQUAL_MESSAGE(v.length, n, v.name);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v.bytes, again, n, v.name);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_phone_keyboard_characters_become_ones_the_font_has);
  RUN_TEST(test_characters_with_no_plain_form_and_long_text_are_refused);
  RUN_TEST(test_letters_are_as_wide_as_their_ink_one_column_apart);
  RUN_TEST(test_text_scrolls_in_from_the_right_and_leaves_a_gap_of_plain_color);
  RUN_TEST(test_a_turn_puts_the_tops_of_the_letters_on_another_edge);
  RUN_TEST(test_auto_draws_dark_letters_on_light_colors_and_white_on_dark_ones);
  RUN_TEST(test_text_survives_a_restart_and_bad_bytes_are_refused);
  RUN_TEST(test_the_console_sets_shows_and_clears_the_text);
  RUN_TEST(test_text_scrolls_over_the_color_only_on_a_grid_that_can_show_it);
  RUN_TEST(test_a_stale_color_keeps_its_text_and_picks_letters_for_the_dimmed_color);
  RUN_TEST(test_flow_shows_the_liquid_in_the_color_and_text_reads_over_it);
  RUN_TEST(test_text_values_encode_as_published);
  RUN_TEST(test_a_phone_write_is_kept_in_the_font_or_refused_with_a_reason);
  RUN_TEST(test_bad_text_values_are_ignored_and_longer_ones_read);
  RUN_TEST(test_a_read_written_back_changes_nothing);
  return UNITY_END();
}

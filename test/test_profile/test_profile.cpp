#include <profile.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "vectors.h"

using namespace cheerlights;

void setUp() {}
void tearDown() {}

static void test_published_vectors_parse() {
  for (const FrameVector& v : FRAME_VECTORS) {
    Frame f{};
    TEST_ASSERT_TRUE_MESSAGE(parseFrame(v.bytes, FRAME_SIZE, f) == ParseResult::Ok, v.name);
    TEST_ASSERT_EQUAL_MESSAGE(v.stale, f.stale, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.paletteIndex, f.paletteIndex, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.r, f.r, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.g, f.g, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.b, f.b, v.name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v.sequence, f.sequence, v.name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v.ageMinutes, f.ageMinutes, v.name);
  }
}

static void test_published_vectors_encode_back_to_the_same_bytes() {
  for (const FrameVector& v : FRAME_VECTORS) {
    Frame f{};
    parseFrame(v.bytes, FRAME_SIZE, f);
    uint8_t out[FRAME_SIZE];
    encodeFrame(f, out);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(v.bytes, out, FRAME_SIZE, v.name);
  }
}

static void test_short_frames_are_ignored() {
  const uint8_t b[FRAME_SIZE] = {0x10};
  Frame f{};
  TEST_ASSERT_TRUE(parseFrame(b, 9, f) == ParseResult::WrongLength);
  TEST_ASSERT_TRUE(parseFrame(b, 0, f) == ParseResult::WrongLength);
  TEST_ASSERT_TRUE(parseFrame(nullptr, 10, f) == ParseResult::WrongLength);
}

// A newer phone may add fields at the end; this firmware reads the ones it knows (Compatibility).
static void test_longer_frames_from_a_newer_phone_read_the_known_fields() {
  for (const NewerFrameVector& v : FRAME_NEWER) {
    Frame f{};
    TEST_ASSERT_TRUE_MESSAGE(parseFrame(v.bytes, v.length, f) == ParseResult::Ok, v.name);
    TEST_ASSERT_EQUAL_MESSAGE(v.stale, f.stale, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.paletteIndex, f.paletteIndex, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.r, f.r, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.g, f.g, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.b, f.b, v.name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v.sequence, f.sequence, v.name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(v.ageMinutes, f.ageMinutes, v.name);
  }
}

static void test_unknown_version_is_ignored() {
  for (int high : {0, 2, 15}) {
    uint8_t b[FRAME_SIZE] = {};
    b[0] = static_cast<uint8_t>(high << 4);
    Frame f{};
    TEST_ASSERT_TRUE(parseFrame(b, FRAME_SIZE, f) == ParseResult::UnknownVersion);
  }
}

static void test_unknown_flags_do_not_block_a_frame() {
  uint8_t b[FRAME_SIZE] = {0x1E, 9, 1, 2, 3};  // bit 0 clear, other flags set
  Frame f{};
  TEST_ASSERT_TRUE(parseFrame(b, FRAME_SIZE, f) == ParseResult::Ok);
  TEST_ASSERT_FALSE(f.stale);
}

static void test_fields_are_little_endian() {
  const uint8_t b[FRAME_SIZE] = {0x10, 0, 0, 0, 0, 0x34, 0x12, 0x78, 0x56, 0};
  Frame f{};
  TEST_ASSERT_TRUE(parseFrame(b, FRAME_SIZE, f) == ParseResult::Ok);
  TEST_ASSERT_EQUAL_HEX16(0x1234, f.sequence);
  TEST_ASSERT_EQUAL_HEX16(0x5678, f.ageMinutes);
}

static void test_stale_looks_different_from_fresh() {
  Frame f{};
  f.r = 255;
  f.g = 165;
  f.b = 0;
  f.stale = false;
  const Shown fresh = renderFrame(f);
  TEST_ASSERT_EQUAL_UINT8(255, fresh.r);
  TEST_ASSERT_FALSE(fresh.markersOff);
  f.stale = true;
  const Shown stale = renderFrame(f);
  TEST_ASSERT_LESS_THAN_UINT8(fresh.r, stale.r);
  TEST_ASSERT_TRUE(stale.markersOff);
}

static void test_info_vectors_encode() {
  for (const InfoVector& v : INFO_VECTORS) {
    uint8_t out[INFO_MAX_SIZE];
    const size_t n = encodeInfo({v.major, v.minor, v.patch, v.display, v.offControl, v.light, v.needsSetup, v.identify, v.board}, out);
    TEST_ASSERT_EQUAL_MESSAGE(v.length, n, v.name);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v.bytes, out, v.length, v.name);
  }
}

// A long board name is cut to INFO_BOARD_MAX bytes, never inside a character.
static void test_info_cuts_a_long_board_name_at_a_whole_character() {
  uint8_t out[INFO_MAX_SIZE];
  // 39 ASCII bytes, then a three-byte character that would end at byte 42.
  const char* board = "012345678901234567890123456789012345678" "\xe2\x80\x94";
  const size_t n = encodeInfo({0, 5, 0, true, false, true, false, true, board}, out);
  TEST_ASSERT_EQUAL(INFO_SIZE + 1 + 39, n);
  TEST_ASSERT_EQUAL(39, out[INFO_SIZE]);
  const char* fits = "Waveshare ESP32-S3-Matrix, 8x8";
  TEST_ASSERT_EQUAL(INFO_SIZE + 1 + strlen(fits), encodeInfo({0, 5, 0, true, true, true, false, true, fits}, out));
  TEST_ASSERT_EQUAL_MEMORY(fits, out + INFO_SIZE + 1, strlen(fits));
}

static void test_identify_vectors() {
  for (const IdentifyVector& v : IDENTIFY_VECTORS) {
    TEST_ASSERT_EQUAL_MESSAGE(v.ok, parseIdentify(v.bytes, v.length) == ParseResult::Ok, v.name);
  }
  TEST_ASSERT_TRUE(parseIdentify(nullptr, 1) != ParseResult::Ok);
}

static void test_light_vectors() {
  for (const LightVector& v : LIGHT_VECTORS) {
    Light l{};
    TEST_ASSERT_TRUE_MESSAGE(parseLight(v.bytes, v.length, l) == ParseResult::Ok, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.brightness, l.brightness, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.effect, l.effect, v.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.speed, l.speed, v.name);
  }
  for (const LightVector& v : LIGHT_REJECTED) {
    Light l{};
    TEST_ASSERT_TRUE_MESSAGE(parseLight(v.bytes, v.length, l) != ParseResult::Ok, v.name);
  }
}

static void test_light_round_trips() {
  Light in{};
  in.brightness = 77;
  in.effect = EFFECT_RIPPLE;
  in.speed = 200;
  uint8_t b[LIGHT_SIZE];
  encodeLight(in, b);
  Light out{};
  TEST_ASSERT_TRUE(parseLight(b, LIGHT_SIZE, out) == ParseResult::Ok);
  TEST_ASSERT_EQUAL_UINT8(77, out.brightness);
  TEST_ASSERT_EQUAL_UINT8(EFFECT_RIPPLE, out.effect);
  TEST_ASSERT_EQUAL_UINT8(200, out.speed);
}

static void test_effects_list_matches_the_published_vector() {
  uint8_t b[128];
  const size_t length = encodeEffects(EFFECTS, EFFECT_COUNT, b, sizeof(b));
  TEST_ASSERT_EQUAL_UINT(EFFECTS_VECTOR_LENGTH, length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(EFFECTS_VECTOR, b, length);
  TEST_ASSERT_EQUAL_UINT(0, encodeEffects(EFFECTS, EFFECT_COUNT, b, 10));
}

static void test_solid_and_unknown_effects_show_the_color() {
  const Rgb8 orange{255, 165, 0};
  for (uint8_t effect : {static_cast<uint8_t>(EFFECT_SOLID), static_cast<uint8_t>(200)}) {
    const Rgb8 p = effectPixel(effect, orange, 128, 12345, 3, 4, 8, 8);
    TEST_ASSERT_EQUAL_UINT8(255, p.r);
    TEST_ASSERT_EQUAL_UINT8(165, p.g);
    TEST_ASSERT_EQUAL_UINT8(0, p.b);
    TEST_ASSERT_FALSE(effectMoves(effect));
  }
}

static void test_breathe_stays_visible_and_reaches_full() {
  const Rgb8 red{255, 0, 0};
  uint8_t lowest = 255, highest = 0;
  for (uint32_t ms = 0; ms < 6000; ms += 20) {
    const Rgb8 p = effectPixel(EFFECT_BREATHE, red, 1, ms, 0, 0, 1, 1);
    if (p.r < lowest) lowest = p.r;
    if (p.r > highest) highest = p.r;
    TEST_ASSERT_EQUAL_UINT8(0, p.g);
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(38, lowest);  // 15% of 255, never dark
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(250, highest);
}

// The flag a phone relies on matches what each effect draws: an effect that says it shows the color
// keeps blue blue on every pixel, and one that does not paints other hues.
static void test_shows_color_flag_matches_each_effect() {
  const Rgb8 blue{0, 0, 255};
  for (size_t i = 0; i < EFFECT_COUNT; i++) {
    const EffectInfo& e = EFFECTS[i];
    bool otherHue = false;
    for (uint32_t ms = 0; ms < 3000; ms += 97) {
      for (int x = 0; x < 8; x++) {
        const Rgb8 p = effectPixel(e.id, blue, 128, ms, x, (ms / 8) % 8, 8, 8);
        if (p.r != 0 || p.g != 0) otherHue = true;
      }
    }
    TEST_ASSERT_EQUAL_MESSAGE((e.flags & EFFECT_SHOWS_COLOR) == 0, otherHue, e.name);
    TEST_ASSERT_EQUAL_MESSAGE(e.id != EFFECT_SOLID, effectMoves(e.id), e.name);
  }
}

static void test_faster_speed_changes_sooner() {
  const Rgb8 white{255, 255, 255};
  const Rgb8 slow = effectPixel(EFFECT_BREATHE, white, 1, 300, 0, 0, 8, 8);
  const Rgb8 fast = effectPixel(EFFECT_BREATHE, white, 255, 300, 0, 0, 8, 8);
  TEST_ASSERT_GREATER_THAN_UINT8(slow.r, fast.r);
}

// The 8x8 wearable's rainbow is unchanged by the general layout: 22.5 degrees per step.
static void test_rainbow_on_8x8_steps_22_5_degrees() {
  const Rgb8 any{0, 0, 0};
  const Rgb8 origin = effectPixel(EFFECT_RAINBOW, any, 128, 0, 0, 0, 8, 8);
  TEST_ASSERT_EQUAL_UINT8(255, origin.r);
  TEST_ASSERT_EQUAL_UINT8(0, origin.g);
  // Four steps is 90 degrees: hue 90 is (127, 255, 0).
  const Rgb8 quarter = effectPixel(EFFECT_RAINBOW, any, 128, 0, 2, 2, 8, 8);
  TEST_ASSERT_UINT8_WITHIN(1, 127, quarter.r);
  TEST_ASSERT_EQUAL_UINT8(255, quarter.g);
}

// A single LED and a strip get every effect, without dividing by zero or going dark.
static void test_effects_work_on_a_single_led_and_a_strip() {
  const Rgb8 green{0, 255, 0};
  for (const EffectInfo& e : {EFFECTS[0], EFFECTS[1], EFFECTS[2], EFFECTS[3], EFFECTS[4]}) {
    uint32_t total = 0;
    for (uint32_t ms = 0; ms < 6000; ms += 50) {
      const Rgb8 one = effectPixel(e.id, green, 1, ms, 0, 0, 1, 1);
      total += one.r + one.g + one.b;
      const Rgb8 end = effectPixel(e.id, green, 1, ms, 59, 0, 60, 1);
      total += end.r + end.g + end.b;
    }
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, total, e.name);
  }
  // On a strip, the rainbow covers the whole wheel from one end to the other.
  const Rgb8 first = effectPixel(EFFECT_RAINBOW, green, 128, 0, 0, 0, 60, 1);
  const Rgb8 middle = effectPixel(EFFECT_RAINBOW, green, 128, 0, 30, 0, 60, 1);
  TEST_ASSERT_EQUAL_UINT8(255, first.r);
  TEST_ASSERT_LESS_THAN_UINT8(10, middle.r);  // hue about 177, cyan
}

// Tester report, 2026-11-09: pink looked almost white on the LEDs. With gamma, its green and blue drop well below
// its red, so it reads as pink; the ends stay put and the curve never runs backwards.
static void test_led_gamma_keeps_the_ends_and_makes_pink_pink() {
  TEST_ASSERT_EQUAL_UINT8(0, ledLevel(0));
  TEST_ASSERT_EQUAL_UINT8(255, ledLevel(255));
  for (int i = 1; i < 256; i++) TEST_ASSERT_TRUE(ledLevel(static_cast<uint8_t>(i)) >= ledLevel(static_cast<uint8_t>(i - 1)));
  const Rgb8 pink = ledColor({0xff, 0xc0, 0xcb});
  TEST_ASSERT_EQUAL_UINT8(255, pink.r);
  TEST_ASSERT_INT_WITHIN(2, 115, pink.g);
  TEST_ASSERT_INT_WITHIN(2, 135, pink.b);
  // Palette primaries are unchanged: full channels stay full and empty ones stay off.
  const Rgb8 magenta = ledColor({0xff, 0x00, 0xff});
  TEST_ASSERT_TRUE(magenta.r == 255 && magenta.g == 0 && magenta.b == 255);
  // Green (008000) is half on the wire, so it becomes much dimmer; orange (ffa500) moves toward red.
  TEST_ASSERT_INT_WITHIN(2, 37, ledColor({0x00, 0x80, 0x00}).g);
  TEST_ASSERT_INT_WITHIN(2, 75, ledColor({0xff, 0xa5, 0x00}).g);
}

// The palette colors an LED already shows well stay as they are; the rest were picked by eye (2026-11-09).
static void test_led_palette_keeps_pure_colors_and_frames_pick_it_by_index() {
  const uint32_t expected[PALETTE_SIZE] = {0xff0000, 0x00ff00, 0x0000ff, 0x00ffff, 0xffffff, 0xff9040,
                                           0x6000c0, 0xff00ff, 0xffc000, 0xff4000, 0xff2454};
  for (size_t i = 0; i < PALETTE_SIZE; i++) {
    const Rgb8 c = LED_PALETTE[i];
    TEST_ASSERT_EQUAL_HEX32(expected[i], (static_cast<uint32_t>(c.r) << 16) | (c.g << 8) | c.b);
  }
  Frame f{};
  f.paletteIndex = 9;  // orange
  f.r = 0xff;
  f.g = 0xa5;
  const Rgb8 orange = ledBase(f);
  TEST_ASSERT_TRUE(orange.r == 0xff && orange.g == 0x40 && orange.b == 0);
  f.paletteIndex = PALETTE_SIZE;  // past the palette: not a palette color, so gamma
  TEST_ASSERT_INT_WITHIN(2, 75, ledBase(f).g);
  f.paletteIndex = PALETTE_NONE;
  TEST_ASSERT_INT_WITHIN(2, 75, ledBase(f).g);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_published_vectors_parse);
  RUN_TEST(test_published_vectors_encode_back_to_the_same_bytes);
  RUN_TEST(test_short_frames_are_ignored);
  RUN_TEST(test_longer_frames_from_a_newer_phone_read_the_known_fields);
  RUN_TEST(test_unknown_version_is_ignored);
  RUN_TEST(test_unknown_flags_do_not_block_a_frame);
  RUN_TEST(test_fields_are_little_endian);
  RUN_TEST(test_stale_looks_different_from_fresh);
  RUN_TEST(test_info_vectors_encode);
  RUN_TEST(test_info_cuts_a_long_board_name_at_a_whole_character);
  RUN_TEST(test_identify_vectors);
  RUN_TEST(test_light_vectors);
  RUN_TEST(test_light_round_trips);
  RUN_TEST(test_effects_list_matches_the_published_vector);
  RUN_TEST(test_solid_and_unknown_effects_show_the_color);
  RUN_TEST(test_breathe_stays_visible_and_reaches_full);
  RUN_TEST(test_shows_color_flag_matches_each_effect);
  RUN_TEST(test_faster_speed_changes_sooner);
  RUN_TEST(test_rainbow_on_8x8_steps_22_5_degrees);
  RUN_TEST(test_effects_work_on_a_single_led_and_a_strip);
  RUN_TEST(test_led_gamma_keeps_the_ends_and_makes_pink_pink);
  RUN_TEST(test_led_palette_keeps_pure_colors_and_frames_pick_it_by_index);
  return UNITY_END();
}

#include <flow.h>
#include <math.h>
#include <stdio.h>
#include <unity.h>

using namespace cheerlights;

void setUp() {}
void tearDown() {}

static void run(Flow& f, float gx, float gy, float seconds, uint8_t speed = 128) {
  const int steps = static_cast<int>(seconds / FLOW_STEP_S + 0.5f);
  for (int i = 0; i < steps; i++) f.step(gx, gy, speed);
}

static float total(const Flow& f) {
  float sum = 0;
  for (int y = 0; y < f.height(); y++) {
    for (int x = 0; x < f.width(); x++) sum += f.level(x, y);
  }
  return sum;
}

static float centerY(const Flow& f) {
  float sum = 0;
  for (int i = 0; i < f.drops(); i++) sum += f.drop(i).y;
  return sum / f.drops();
}

static float row(const Flow& f, int y) {
  float sum = 0;
  for (int x = 0; x < f.width(); x++) sum += f.level(x, y);
  return sum / f.width();
}

static float column(const Flow& f, int x) {
  float sum = 0;
  for (int y = 0; y < f.height(); y++) sum += f.level(x, y);
  return sum / f.height();
}

static float fastest(const Flow& f) {
  float top = 0;
  for (int i = 0; i < f.drops(); i++) top = fmaxf(top, sqrtf(f.drop(i).vx * f.drop(i).vx + f.drop(i).vy * f.drop(i).vy));
  return top;
}

// The picture, for reading in the test output: # full, + more than half, . a little, space empty.
static void show(const Flow& f, const char* label) {
  printf("%s\n", label);
  for (int y = 0; y < f.height(); y++) {
    char line[FLOW_MAX_CELLS + 1];
    for (int x = 0; x < f.width(); x++) {
      const float v = f.level(x, y);
      line[x] = v > 0.85f ? '#' : v > 0.5f ? '+' : v > 0.1f ? '.' : ' ';
    }
    line[f.width()] = '\0';
    printf("  |%s|\n", line);
  }
}

static void test_it_starts_half_full_at_the_bottom_and_at_rest() {
  Flow f;
  f.reset(8, 8);
  TEST_ASSERT_EQUAL(32, f.drops());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.0f, total(f));  // at rest on the pixels' centers, each drop lights one pixel
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, row(f, 7));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, row(f, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, fastest(f));
}

// Held upright the liquid lies along the bottom; tipped right it runs to the right side; upside down, to the top.
static void test_the_liquid_runs_to_whichever_side_is_down() {
  Flow f;
  f.reset(8, 8);
  run(f, 0, 1, 3);
  show(f, "upright");
  TEST_ASSERT_TRUE(row(f, 7) > 0.85f);
  TEST_ASSERT_TRUE(row(f, 0) < 0.05f);
  run(f, 1, 0, 3);
  show(f, "right side down");
  TEST_ASSERT_TRUE(column(f, 7) > 0.85f);
  TEST_ASSERT_TRUE(column(f, 0) < 0.05f);
  run(f, 0, -1, 3);
  show(f, "upside down");
  TEST_ASSERT_TRUE(row(f, 0) > 0.85f);
  TEST_ASSERT_TRUE(row(f, 7) < 0.05f);
  run(f, 0.71f, 0.71f, 3);
  show(f, "tipped 45 degrees right");
  // The bottom right corner fills and the top left empties.
  TEST_ASSERT_TRUE(f.level(7, 7) > 0.85f);
  TEST_ASSERT_TRUE(f.level(0, 0) < 0.05f);
}

// Every drop stays inside, the liquid comes to rest without piling into one pixel, and its body is solid: about as
// many pixels are lit as there are drops, with no dim holes.
static void test_the_liquid_keeps_its_amount_settles_and_does_not_squash() {
  Flow f;
  f.reset(8, 8);
  const float gs[][2] = {{0, 1}, {1, 0}, {-0.5f, -0.9f}, {0.3f, 0.95f}};
  for (const auto& g : gs) {
    run(f, g[0], g[1], 4);
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 32.0f, total(f));
    TEST_ASSERT_TRUE(fastest(f) < 0.5f);
    for (int i = 0; i < f.drops(); i++) {
      TEST_ASSERT_TRUE(f.drop(i).x >= 0.5f && f.drop(i).x <= 7.5f);
      TEST_ASSERT_TRUE(f.drop(i).y >= 0.5f && f.drop(i).y <= 7.5f);
      for (int j = i + 1; j < f.drops(); j++) {
        const float dx = f.drop(i).x - f.drop(j).x, dy = f.drop(i).y - f.drop(j).y;
        TEST_ASSERT_TRUE(dx * dx + dy * dy > 0.6f * 0.6f);
      }
    }
  }
}

// Lying flat, gravity pulls along neither axis, so the liquid stays where it was, as a puddle does on a level table.
static void test_lying_flat_the_liquid_stays_put() {
  Flow f;
  f.reset(8, 8);
  run(f, 0, 1, 3);
  const float before = row(f, 7);
  run(f, 0.01f, -0.02f, 3);  // a sensor's small error
  TEST_ASSERT_FLOAT_WITHIN(0.1f, before, row(f, 7));
}

// A hard shake throws the liquid around but never out of the grid or into nonsense numbers.
static void test_a_hard_shake_keeps_the_liquid_whole() {
  Flow f;
  f.reset(8, 8);
  for (int i = 0; i < 300; i++) {
    const float s = (i / 10) % 2 ? 4.0f : -4.0f;
    f.step(s, -s * 0.7f, 255);
  }
  show(f, "after a shake");
  for (int i = 0; i < f.drops(); i++) {
    TEST_ASSERT_FALSE(isnan(f.drop(i).x) || isnan(f.drop(i).y));
    TEST_ASSERT_TRUE(f.drop(i).x >= 0.5f && f.drop(i).x <= 7.5f);
  }
}

// Slow is thick: from the same start, after the same short time, the thin liquid has fallen further.
static void test_speed_makes_the_liquid_thick_or_thin() {
  Flow slow, quick;
  slow.reset(8, 8);
  quick.reset(8, 8);
  // Turned upside down, the liquid falls toward the top row.
  run(slow, 0, -1, 0.3f, 1);
  run(quick, 0, -1, 0.3f, 255);
  printf("fallen after 0.3 s: slow %.2f, quick %.2f pixels\n", 5.5f - centerY(slow), 5.5f - centerY(quick));
  TEST_ASSERT_TRUE(centerY(quick) < centerY(slow) - 1.0f);
}

// The sensor's readings on the Matrix, measured 2026-11-20, give gravity along the grid.
static void test_the_matrix_sensor_reading_becomes_gravity_on_the_grid() {
  float gx = 0, gy = 0;
  matrixGravity(1.02f, 0.0f, gx, gy);  // held upright
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, gx);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.02f, gy);
  matrixGravity(0.72f, -0.71f, gx, gy);  // tipped with the right side down
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.71f, gx);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.72f, gy);
}

// A bigger grid gets more drops, up to the most it can hold.
static void test_grids_of_other_sizes_work() {
  Flow f;
  f.reset(16, 16);
  TEST_ASSERT_EQUAL(FLOW_MAX_DROPS, f.drops());
  run(f, 0, 1, 3);
  TEST_ASSERT_TRUE(row(f, 15) > 0.85f);
  f.reset(8, 32);
  run(f, 0, 1, 4);
  TEST_ASSERT_TRUE(row(f, 31) > 0.85f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_it_starts_half_full_at_the_bottom_and_at_rest);
  RUN_TEST(test_the_liquid_runs_to_whichever_side_is_down);
  RUN_TEST(test_the_liquid_keeps_its_amount_settles_and_does_not_squash);
  RUN_TEST(test_lying_flat_the_liquid_stays_put);
  RUN_TEST(test_a_hard_shake_keeps_the_liquid_whole);
  RUN_TEST(test_speed_makes_the_liquid_thick_or_thin);
  RUN_TEST(test_the_matrix_sensor_reading_becomes_gravity_on_the_grid);
  RUN_TEST(test_grids_of_other_sizes_work);
  return UNITY_END();
}

#include "flow.h"

#include <math.h>
#include <string.h>

namespace cheerlights {

namespace {

constexpr float SPACING = 1.0f;       // drops push apart closer than this, so a full pixel holds about one drop
constexpr int PASSES = 3;             // rounds of pushing apart each step, so a deep pile does not squash
constexpr float BOUNCE = 0.25f;       // the share of speed a drop keeps off a wall
constexpr float SMOOTHING = 0.08f;    // drops nearby share their speed, so the liquid moves together
// A tilt under this, in g, moves nothing, as surface tension holds a puddle on a table that is nearly level; it
// also hides the sensor's own small error. Past it the pull grows from zero, so there is no jump.
constexpr float STILL_BELOW = 0.08f;
// How much of a pixel the liquid covers becomes its brightness: thin cover is dark and most cover is full, so the
// body of the liquid is solid and only its surface is soft.
constexpr float COVER_DARK = 0.15f, COVER_FULL = 0.7f;

// Gravity's pull, and how much speed is lost each step, by speed 1 to 255: slow is thick, fast is thin.
// Hans found the first tuning too thick on the Matrix (2026-11-20), so the middle is about twice as quick.
float pull(uint8_t speed) { return 40.0f + 200.0f * (speed - 1) / 254.0f; }  // pixels a second squared, per g
float keep(uint8_t speed) { return 0.97f + 0.027f * (speed - 1) / 254.0f; }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void Flow::reset(int width, int height) {
  width_ = width < 1 ? 1 : width;
  height_ = height < 1 ? 1 : height;
  if (width_ * height_ > FLOW_MAX_CELLS) height_ = FLOW_MAX_CELLS / width_;
  int count = width_ * height_ / 2;
  count_ = count > FLOW_MAX_DROPS ? FLOW_MAX_DROPS : count;
  // Rows from the bottom up, at the pixels' centers, so the liquid starts at rest in the lower half.
  for (int i = 0; i < count_; i++) {
    drops_[i] = {(i % width_) + 0.5f, height_ - 0.5f - static_cast<float>(i / width_), 0, 0};
  }
  cover();
}

void Flow::step(float gx, float gy, uint8_t speed) {
  if (count_ == 0) return;
  if (speed == 0) speed = 1;
  // A hard shake reads as more than gravity; past 2 g it only throws the liquid harder into the walls.
  const float g = sqrtf(gx * gx + gy * gy);
  const float felt = g < STILL_BELOW ? 0.0f : fminf(g, 2.0f) - STILL_BELOW;
  if (g > 0) {
    gx *= felt / g;
    gy *= felt / g;
  }
  const float dt = FLOW_STEP_S, a = pull(speed), k = keep(speed);
  const float lo = 0.5f, hiX = width_ - 0.5f, hiY = height_ - 0.5f;
  float px[FLOW_MAX_DROPS], py[FLOW_MAX_DROPS];
  for (int i = 0; i < count_; i++) {
    FlowDrop& d = drops_[i];
    d.vx = (d.vx + gx * a * dt) * k;
    d.vy = (d.vy + gy * a * dt) * k;
    px[i] = d.x + d.vx * dt;
    py[i] = d.y + d.vy * dt;
  }
  // Push crowded drops apart, half each way, and keep every drop inside the grid.
  for (int pass = 0; pass < PASSES; pass++) {
    for (int i = 0; i < count_; i++) {
      for (int j = i + 1; j < count_; j++) {
        float dx = px[j] - px[i], dy = py[j] - py[i];
        float d2 = dx * dx + dy * dy;
        if (d2 >= SPACING * SPACING) continue;
        float d = sqrtf(d2);
        if (d < 1e-4f) {
          // Two drops in one place: part them along a direction that depends only on which drops they are.
          dx = (((i * 7 + j * 3) % 5) - 2) * 0.1f + 0.05f;
          dy = 0.1f;
          d = sqrtf(dx * dx + dy * dy);
        }
        const float push = 0.5f * (SPACING - d) / d;
        px[i] -= dx * push;
        py[i] -= dy * push;
        px[j] += dx * push;
        py[j] += dy * push;
      }
    }
    for (int i = 0; i < count_; i++) {
      px[i] = clampf(px[i], lo, hiX);
      py[i] = clampf(py[i], lo, hiY);
    }
  }
  // Speed is where each drop got to, so pushing apart moves the liquid as a whole. Off a wall, a drop keeps only a
  // little of its speed into it.
  for (int i = 0; i < count_; i++) {
    FlowDrop& d = drops_[i];
    d.vx = (px[i] - d.x) / dt;
    d.vy = (py[i] - d.y) / dt;
    if ((px[i] <= lo && d.vx < 0) || (px[i] >= hiX && d.vx > 0)) d.vx *= -BOUNCE;
    if ((py[i] <= lo && d.vy < 0) || (py[i] >= hiY && d.vy > 0)) d.vy *= -BOUNCE;
    d.x = px[i];
    d.y = py[i];
  }
  // Neighbors share speed, which reads as liquid rather than loose grains.
  float svx[FLOW_MAX_DROPS], svy[FLOW_MAX_DROPS];
  for (int i = 0; i < count_; i++) {
    svx[i] = 0;
    svy[i] = 0;
    for (int j = 0; j < count_; j++) {
      if (j == i) continue;
      const float dx = drops_[j].x - drops_[i].x, dy = drops_[j].y - drops_[i].y;
      const float d2 = dx * dx + dy * dy;
      if (d2 >= 1.5f * 1.5f * SPACING * SPACING) continue;
      const float w = 1.0f - sqrtf(d2) / (1.5f * SPACING);
      svx[i] += (drops_[j].vx - drops_[i].vx) * w;
      svy[i] += (drops_[j].vy - drops_[i].vy) * w;
    }
  }
  for (int i = 0; i < count_; i++) {
    drops_[i].vx += SMOOTHING * svx[i];
    drops_[i].vy += SMOOTHING * svy[i];
  }
  cover();
}

void Flow::cover() {
  memset(level_, 0, sizeof(level_));
  // Each drop spreads over the four pixels nearest its center, by how near it is to each.
  for (int i = 0; i < count_; i++) {
    const float u = drops_[i].x - 0.5f, v = drops_[i].y - 0.5f;
    const int x0 = static_cast<int>(floorf(u)), y0 = static_cast<int>(floorf(v));
    const float fx = u - x0, fy = v - y0;
    const float w[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    const int cx[4] = {x0, x0 + 1, x0, x0 + 1}, cy[4] = {y0, y0, y0 + 1, y0 + 1};
    for (int k = 0; k < 4; k++) {
      if (cx[k] < 0 || cx[k] >= width_ || cy[k] < 0 || cy[k] >= height_) continue;
      level_[cy[k] * width_ + cx[k]] += w[k];
    }
  }
}

float Flow::level(int x, int y) const {
  if (x < 0 || x >= width_ || y < 0 || y >= height_) return 0;
  const float v = (level_[y * width_ + x] - COVER_DARK) / (COVER_FULL - COVER_DARK);
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void matrixGravity(float sensorX, float sensorY, float& gx, float& gy) {
  gx = -sensorY;
  gy = sensorX;
}

}  // namespace cheerlights

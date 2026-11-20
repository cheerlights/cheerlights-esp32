// Flow: a little liquid in the light's color that runs where the board tips it, on a grid of LEDs with a motion
// sensor. Drops of liquid are particles with positions finer than a pixel; each step, gravity pulls them, they push
// apart where they crowd, and the walls hold them in. A pixel is as bright as the liquid covering it, so the surface
// moves smoothly between pixels. No hardware dependencies, so the native tests cover all of it.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cheerlights {

constexpr int FLOW_MAX_DROPS = 96;
constexpr int FLOW_MAX_CELLS = 512;
// The step the simulation takes, in seconds; the light runs as many as the time that passed calls for.
constexpr float FLOW_STEP_S = 0.01f;

struct FlowDrop {
  float x, y;    // in pixels, from the grid's top left corner; a pixel's center is at its index plus a half
  float vx, vy;  // pixels a second
};

class Flow {
 public:
  // Fills the bottom of a width by height grid half full, at rest.
  void reset(int width, int height);
  // One step of FLOW_STEP_S. gx and gy are the pull of gravity along the grid's columns and rows, in g: lying flat
  // both are near zero, and held upright with the first row at the top gy is 1. speed 1 to 255 makes the liquid
  // thick and slow, or thin and quick.
  void step(float gx, float gy, uint8_t speed);
  // How brightly the pixel at column x, row y shows the liquid, 0 to 1: full inside it, soft at its surface.
  float level(int x, int y) const;

  int width() const { return width_; }
  int height() const { return height_; }
  int drops() const { return count_; }
  const FlowDrop& drop(int i) const { return drops_[i]; }

 private:
  void cover();

  int width_ = 0, height_ = 0, count_ = 0;
  FlowDrop drops_[FLOW_MAX_DROPS];
  float level_[FLOW_MAX_CELLS];
};

// Gravity on the grid from the motion sensor's reading, for the Waveshare ESP32-S3-Matrix: held upright, as text
// reads, the sensor's x axis points up, and its y axis points right (measured on the board, 2026-11-20). A still accelerometer reads the push of
// whatever holds it up, so gravity is the opposite of the reading.
void matrixGravity(float sensorX, float sensorY, float& gx, float& gy);

}  // namespace cheerlights

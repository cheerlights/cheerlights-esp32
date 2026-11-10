// The animations over the CheerLights color. The right button steps through them.
// Each scene is a set of particles moved by sceneStep and drawn by sceneDraw, from a seed, so a run is repeatable.
#pragma once
#include <stdint.h>

#include "canvas.h"

namespace cheerlights {

enum class SceneId : uint8_t { Solid = 0, Snow = 1, Leaves = 2, Sparkle = 3 };
constexpr int SCENE_COUNT = 4;

const char* sceneName(SceneId id);  // "solid", "snow", "leaves", "sparkle"
bool sceneFromName(const char* name, SceneId& out);
SceneId nextScene(SceneId id);

struct Particle {
  float x, y;     // position; for snow, x is the center of the sway
  float vy;       // fall speed, pixels a second
  float size;     // radius or length
  float phase;    // where in its sway, turn, or twinkle it is
  float rate;     // how fast that cycle runs
  float spin;     // leaves: turns a second
  uint8_t layer;  // snow: 0 far to 2 near; leaves: which autumn tone
};

constexpr int MAX_PARTICLES = 80;

struct SceneState {
  SceneId id;
  int width, height;
  uint32_t rng;
  float t;  // seconds since the scene started
  int count;
  Particle p[MAX_PARTICLES];
};

void sceneStart(SceneState& s, SceneId id, int width, int height, uint32_t seed);
void sceneStep(SceneState& s, float dt);
void sceneDraw(const SceneState& s, Canvas& canvas, Rgb background);
bool sceneMoves(SceneId id);

}  // namespace cheerlights

#include "scenes.h"

#include <math.h>
#include <string.h>

#include "color.h"

namespace cheerlights {

namespace {

constexpr float PI_F = 3.14159265f;
const char* const NAMES[SCENE_COUNT] = {"solid", "snow", "leaves", "sparkle"};
constexpr int SNOW_COUNT = 70, LEAF_COUNT = 18, SPARKLE_COUNT = 26;

// Autumn tones for leaves: maple red, orange, gold, and brown.
const Rgb LEAF_TONES[] = {{0xc4, 0x36, 0x1b}, {0xe8, 0x76, 0x1c}, {0xe9, 0xb8, 0x2a}, {0x8a, 0x4b, 0x1f}};
constexpr int TONE_COUNT = sizeof(LEAF_TONES) / sizeof(LEAF_TONES[0]);

float rnd(SceneState& s, float lo, float hi) {
  s.rng ^= s.rng << 13;
  s.rng ^= s.rng >> 17;
  s.rng ^= s.rng << 5;
  return lo + (hi - lo) * static_cast<float>(s.rng & 0xFFFFFF) / static_cast<float>(0x1000000);
}

void newFlake(SceneState& s, Particle& p, bool anywhere) {
  p.layer = static_cast<uint8_t>(rnd(s, 0, 3));
  const float depth = p.layer / 2.0f;  // 0 far, 1 near
  p.size = 0.9f + depth * 1.5f + rnd(s, 0, 0.4f);
  p.vy = 12 + depth * 26 + rnd(s, 0, 6);
  p.x = rnd(s, -8, s.width + 8);
  p.y = anywhere ? rnd(s, 0, s.height) : -p.size - rnd(s, 0, 20);
  p.phase = rnd(s, 0, 2 * PI_F);
  p.rate = rnd(s, 0.5f, 1.2f);
}

void newLeaf(SceneState& s, Particle& p, bool anywhere) {
  p.layer = static_cast<uint8_t>(rnd(s, 0, TONE_COUNT));
  p.size = rnd(s, 17, 27);
  p.vy = rnd(s, 16, 30);
  // New leaves start above the top, shifted left, since the wind carries them right as they fall.
  p.x = anywhere ? rnd(s, -10, s.width) : rnd(s, -60, s.width - 40);
  p.y = anywhere ? rnd(s, -10, s.height) : -p.size - rnd(s, 0, s.height);  // staggered, so they never come in waves
  p.phase = rnd(s, 0, 2 * PI_F);
  p.rate = rnd(s, 0.6f, 1.4f);
  p.spin = rnd(s, 0.15f, 0.5f) * (rnd(s, 0, 1) < 0.5f ? -1 : 1);
}

void newSparkle(SceneState& s, Particle& p, bool anywhere) {
  p.x = rnd(s, 4, s.width - 4);
  p.y = rnd(s, 4, s.height - 4);
  p.size = rnd(s, 3, 8);
  p.rate = 1 / rnd(s, 1.2f, 2.6f);
  p.phase = anywhere ? rnd(s, 0, 1) : 0;
}

float distance(Rgb a, Rgb b) {
  const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return sqrtf(dr * dr + dg * dg + db * db);
}

// The tone a leaf was given, or the next one along that stands out from the background.
Rgb leafTone(uint8_t first, Rgb background) {
  for (int i = 0; i < TONE_COUNT; i++) {
    const Rgb t = LEAF_TONES[(first + i) % TONE_COUNT];
    if (distance(t, background) > 120) return t;
  }
  return accentFor(background);
}

}  // namespace

const char* sceneName(SceneId id) {
  const int i = static_cast<int>(id);
  return i < SCENE_COUNT ? NAMES[i] : "solid";
}

bool sceneFromName(const char* name, SceneId& out) {
  for (int i = 0; i < SCENE_COUNT; i++) {
    if (strcmp(NAMES[i], name) == 0) {
      out = static_cast<SceneId>(i);
      return true;
    }
  }
  return false;
}

SceneId nextScene(SceneId id) { return static_cast<SceneId>((static_cast<int>(id) + 1) % SCENE_COUNT); }

bool sceneMoves(SceneId id) { return id != SceneId::Solid; }

void sceneStart(SceneState& s, SceneId id, int width, int height, uint32_t seed) {
  s.id = id;
  s.width = width;
  s.height = height;
  s.rng = seed != 0 ? seed : 0x9E3779B9u;
  s.t = 0;
  s.count = 0;
  switch (id) {
    case SceneId::Snow:
      s.count = SNOW_COUNT;
      for (int i = 0; i < s.count; i++) newFlake(s, s.p[i], true);
      break;
    case SceneId::Leaves:
      s.count = LEAF_COUNT;
      for (int i = 0; i < s.count; i++) newLeaf(s, s.p[i], true);
      break;
    case SceneId::Sparkle:
      s.count = SPARKLE_COUNT;
      for (int i = 0; i < s.count; i++) newSparkle(s, s.p[i], true);
      break;
    default:
      break;
  }
}

void sceneStep(SceneState& s, float dt) {
  if (dt > 0.1f) dt = 0.1f;  // after a stall, carry on instead of jumping
  s.t += dt;
  for (int i = 0; i < s.count; i++) {
    Particle& p = s.p[i];
    switch (s.id) {
      case SceneId::Snow:
        p.y += p.vy * dt;
        p.phase += p.rate * dt;
        if (p.y - p.size > s.height) newFlake(s, p, false);
        break;
      case SceneId::Leaves:
        // A leaf drifts with the wind, sways side to side, and slows as it swings, like a falling leaf.
        p.phase += p.rate * dt;
        p.x += (8 + 22 * cosf(p.phase)) * dt;
        p.y += p.vy * (0.6f + 0.4f * fabsf(sinf(p.phase))) * dt;
        if (p.y - p.size > s.height || p.x - p.size > s.width) newLeaf(s, p, false);
        break;
      case SceneId::Sparkle:
        p.phase += p.rate * dt;
        if (p.phase >= 1) newSparkle(s, p, false);
        break;
      default:
        break;
    }
  }
}

void sceneDraw(const SceneState& s, Canvas& canvas, Rgb background) {
  canvas.fill(background);
  const Rgb accent = accentFor(background);
  switch (s.id) {
    case SceneId::Snow:
      // Far flakes first, so near ones pass in front.
      for (int layer = 0; layer < 3; layer++) {
        const float opacity = 0.55f + layer * 0.22f;
        for (int i = 0; i < s.count; i++) {
          const Particle& p = s.p[i];
          if (p.layer != layer) continue;
          const float sway = (4 + layer * 4) * sinf(p.phase * 2 * PI_F / 2);
          canvas.disc(p.x + sway, p.y, p.size, accent, opacity);
        }
      }
      break;
    case SceneId::Leaves:
      for (int i = 0; i < s.count; i++) {
        const Particle& p = s.p[i];
        const Rgb tone = leafTone(p.layer, background);
        const float angle = 0.6f * sinf(p.phase) + p.spin * s.t * 2 * PI_F;
        const float squash = cosf(p.phase * 1.7f + i);
        canvas.leaf(p.x, p.y, p.size, angle, 0.35f + 0.65f * fabsf(squash), tone, mix(tone, Rgb{0, 0, 0}, 0.4f));
      }
      break;
    case SceneId::Sparkle:
      for (int i = 0; i < s.count; i++) {
        const Particle& p = s.p[i];
        const float glow = sinf(PI_F * p.phase);
        canvas.twinkle(p.x, p.y, p.size * (0.5f + 0.5f * glow), accent, glow * glow);
      }
      break;
    default:
      break;
  }
}

}  // namespace cheerlights

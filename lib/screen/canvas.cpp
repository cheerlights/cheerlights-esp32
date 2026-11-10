#include "canvas.h"

#include <math.h>

namespace cheerlights {

namespace {
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
// The frame holds each pixel high byte first, as the screen takes it.
inline uint16_t wire(uint16_t p) { return static_cast<uint16_t>((p >> 8) | (p << 8)); }
uint8_t lerp8(uint8_t a, uint8_t b, float t) { return static_cast<uint8_t>(a + (b - a) * t + 0.5f); }
}  // namespace

uint16_t to565(Rgb c) { return static_cast<uint16_t>(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3)); }

Rgb from565(uint16_t p) {
  const uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
  return {static_cast<uint8_t>((r << 3) | (r >> 2)), static_cast<uint8_t>((g << 2) | (g >> 4)),
          static_cast<uint8_t>((b << 3) | (b >> 2))};
}

Rgb mix(Rgb a, Rgb b, float t) {
  t = clamp01(t);
  return {lerp8(a.r, b.r, t), lerp8(a.g, b.g, t), lerp8(a.b, b.b, t)};
}

Rgb Canvas::get(int x, int y) const { return from565(wire(px_[y * w_ + x])); }

void Canvas::fill(Rgb c) {
  const uint16_t p = wire(to565(c));
  for (int i = 0; i < w_ * h_; i++) px_[i] = p;
}

void Canvas::fillRect(int x, int y, int w, int h, Rgb c) {
  const uint16_t p = wire(to565(c));
  for (int yy = y < 0 ? 0 : y; yy < y + h && yy < h_; yy++) {
    for (int xx = x < 0 ? 0 : x; xx < x + w && xx < w_; xx++) px_[yy * w_ + xx] = p;
  }
}

void Canvas::mix565(int x, int y, uint16_t c, uint32_t weight) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_ || weight == 0) return;
  uint16_t& p = px_[y * w_ + x];
  if (weight >= 32) {
    p = wire(c);
    return;
  }
  // Spread green apart from red and blue so one multiply blends all three channels.
  const uint16_t under = wire(p);
  const uint32_t fg = (c | (static_cast<uint32_t>(c) << 16)) & 0x07E0F81Fu;
  const uint32_t bg = (under | (static_cast<uint32_t>(under) << 16)) & 0x07E0F81Fu;
  const uint32_t out = ((((fg - bg) * weight) >> 5) + bg) & 0x07E0F81Fu;
  p = wire(static_cast<uint16_t>(out | (out >> 16)));
}

namespace {
uint32_t weightOf(float coverage) { return coverage <= 0 ? 0 : (coverage >= 1 ? 32 : static_cast<uint32_t>(coverage * 32 + 0.5f)); }
}  // namespace

void Canvas::blend(int x, int y, Rgb c, float coverage) { mix565(x, y, to565(c), weightOf(coverage)); }

void Canvas::disc(float cx, float cy, float r, Rgb c, float opacity) {
  const uint16_t c565 = to565(c);
  const int x0 = static_cast<int>(floorf(cx - r - 1)), x1 = static_cast<int>(ceilf(cx + r + 1));
  const int y0 = static_cast<int>(floorf(cy - r - 1)), y1 = static_cast<int>(ceilf(cy + r + 1));
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      const float d2 = dx * dx + dy * dy;
      if (d2 >= (r + 0.5f) * (r + 0.5f)) continue;
      mix565(x, y, c565, weightOf(clamp01(r + 0.5f - sqrtf(d2)) * opacity));
    }
  }
}

void Canvas::leaf(float cx, float cy, float len, float angle, float squash, Rgb fill, Rgb vein) {
  const float half = len / 2, stem = len * 0.2f;
  const float cs = cosf(angle), sn = sinf(angle);
  const float widest = len * 0.3f * fabsf(squash);
  // The box around the turned leaf, so a thin or edge-on leaf scans few pixels.
  const float along = half + stem + 1, across = widest + 1;
  const float ex = fabsf(cs) * along + fabsf(sn) * across, ey = fabsf(sn) * along + fabsf(cs) * across;
  const uint16_t fill565 = to565(fill), vein565 = to565(vein);
  const float veinStrength = clamp01(fabsf(squash) * 2 - 0.3f);
  const int x0 = static_cast<int>(floorf(cx - ex)), x1 = static_cast<int>(ceilf(cx + ex));
  const int y0 = static_cast<int>(floorf(cy - ey)), y1 = static_cast<int>(ceilf(cy + ey));
  for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < h_; y++) {
    for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w_; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      const float v = -dx * sn + dy * cs;  // across the leaf
      if (fabsf(v) > across) continue;
      const float u = dx * cs + dy * sn;  // along it
      if (u > -half && u < half) {
        const float k = u / half;
        const float hw = widest * (1 - k * k);
        const float body = clamp01(hw + 0.5f - fabsf(v));
        if (body <= 0) continue;
        mix565(x, y, fill565, weightOf(body));
        // The vein fades as the leaf turns edge-on.
        if (fabsf(k) < 0.85f && fabsf(v) < 0.9f) mix565(x, y, vein565, weightOf((0.9f - fabsf(v)) * veinStrength));
      } else if (u >= half && u < half + stem && fabsf(v) < 0.8f) {
        mix565(x, y, vein565, weightOf(0.8f - fabsf(v)));
      }
    }
  }
}

void Canvas::twinkle(float cx, float cy, float r, Rgb c, float opacity) {
  if (r <= 0 || opacity <= 0) return;
  const int x0 = static_cast<int>(floorf(cx - r - 1)), x1 = static_cast<int>(ceilf(cx + r + 1));
  const int y0 = static_cast<int>(floorf(cy - r - 1)), y1 = static_cast<int>(ceilf(cy + r + 1));
  const float core = r * 0.3f;
  const uint16_t c565 = to565(c);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float dx = fabsf(x + 0.5f - cx), dy = fabsf(y + 0.5f - cy);
      // Only the core and the two rays are lit; skip the rest of the square quickly.
      if (dx > core + 1 && dy > core + 1 && dx > 1.5f && dy > 1.5f) continue;
      float cov = clamp01(core + 0.5f - sqrtf(dx * dx + dy * dy));
      if (dx < r) cov = fmaxf(cov, clamp01(r * 0.1f * (1 - dx / r) + 0.6f - dy) * (1 - dx / r));
      if (dy < r) cov = fmaxf(cov, clamp01(r * 0.1f * (1 - dy / r) + 0.6f - dx) * (1 - dy / r));
      mix565(x, y, c565, weightOf(cov * opacity));
    }
  }
}

void Canvas::text(const Font& font, int x, int y, const char* s, Rgb c) {
  const uint16_t c565 = to565(c);
  for (; *s; s++) {
    const uint8_t ch = static_cast<uint8_t>(*s);
    if (ch < font.first || ch > font.last) continue;
    const Glyph& g = font.glyphs[ch - font.first];
    for (int gy = 0; gy < g.height; gy++) {
      for (int gx = 0; gx < g.width; gx++) {
        const int i = gy * g.width + gx;
        const uint8_t byte = font.alpha[g.offset + i / 2];
        const uint8_t a = (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
        if (a) mix565(x + g.left + gx, y - g.top + gy, c565, (a * 32 + 7) / 15);
      }
    }
    x += g.advance;
  }
}

int textWidth(const Font& font, const char* s) {
  int w = 0;
  for (; *s; s++) {
    const uint8_t ch = static_cast<uint8_t>(*s);
    if (ch >= font.first && ch <= font.last) w += font.glyphs[ch - font.first].advance;
  }
  return w;
}

}  // namespace cheerlights

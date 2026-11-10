#include "color.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>

namespace cheerlights {

namespace {
int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

double linear(uint8_t channel) {
  const double c = channel / 255.0;
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
}  // namespace

bool parseHex(const char* text, size_t length, Rgb& out) {
  size_t start = 0, end = length;
  while (start < end && isspace(static_cast<unsigned char>(text[start]))) start++;
  while (end > start && isspace(static_cast<unsigned char>(text[end - 1]))) end--;
  if (start < end && text[start] == '#') start++;
  if (end - start != 6) return false;
  uint8_t v[3];
  for (int i = 0; i < 3; i++) {
    const int hi = hexDigit(text[start + i * 2]), lo = hexDigit(text[start + i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    v[i] = static_cast<uint8_t>(hi * 16 + lo);
  }
  out = {v[0], v[1], v[2]};
  return true;
}

const char* paletteName(Rgb c) {
  const uint8_t i = paletteIndexOf(c);
  return i == PALETTE_NONE ? nullptr : PALETTE[i].name;
}

void colorLabel(Rgb c, char out[8]) {
  const char* name = paletteName(c);
  if (name != nullptr) {
    snprintf(out, 8, "%s", name);
  } else {
    snprintf(out, 8, "#%02x%02x%02x", c.r, c.g, c.b);
  }
}

double luminance(Rgb c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }

bool whiteInkFor(Rgb c) {
  const double l = luminance(c);
  return 1.05 / (l + 0.05) > (l + 0.05) / 0.05;
}

Rgb inkFor(Rgb background) { return whiteInkFor(background) ? Rgb{255, 255, 255} : Rgb{0x16, 0x18, 0x1b}; }

Rgb accentFor(Rgb background) {
  return luminance(background) < 0.6 ? Rgb{255, 255, 255} : mix(background, Rgb{0x46, 0x55, 0x6a}, 0.5f);
}

}  // namespace cheerlights

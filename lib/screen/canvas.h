// Drawing into a frame of RGB565 pixels, high byte first, the way the ST7789 screen takes them over SPI,
// so a finished frame goes to the screen as it is.
// Shapes are anti-aliased, so small snowflakes and leaves look smooth at 240 by 135.
// No hardware dependencies: the native tests and the preview tool draw with the same code as the board.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "fonts.h"
#include "profile.h"

namespace cheerlights {

using Rgb = Rgb8;

uint16_t to565(Rgb c);
Rgb from565(uint16_t p);
Rgb mix(Rgb a, Rgb b, float t);  // t from 0 (a) to 1 (b)

class Canvas {
 public:
  Canvas(uint16_t* pixels, int width, int height) : px_(pixels), w_(width), h_(height) {}
  int width() const { return w_; }
  int height() const { return h_; }
  const uint16_t* pixels() const { return px_; }  // each pixel high byte first; get() reads one back
  Rgb get(int x, int y) const;

  void fill(Rgb c);
  void fillRect(int x, int y, int w, int h, Rgb c);
  // Mixes c into one pixel; coverage from 0 to 1. Points outside the frame are ignored.
  void blend(int x, int y, Rgb c, float coverage);
  // A filled circle with a soft one-pixel edge.
  void disc(float cx, float cy, float r, Rgb c, float opacity = 1);
  // A leaf: pointed at both ends, len long, turned by angle (radians), with its width scaled by squash (-1 to 1)
  // so it seems to tumble, and a vein down the middle.
  void leaf(float cx, float cy, float len, float angle, float squash, Rgb fill, Rgb vein);
  // A four-pointed twinkle: a bright core and two crossed rays of radius r.
  void twinkle(float cx, float cy, float r, Rgb c, float opacity);
  // Text with its baseline at y. Characters outside the font draw nothing and advance nothing.
  void text(const Font& font, int x, int y, const char* s, Rgb c);

 private:
  // Mixes a color already in RGB565 into one pixel, by a weight from 0 to 32. The board draws every frame
  // with this, so it stays in integers.
  void mix565(int x, int y, uint16_t c, uint32_t weight);

  uint16_t* px_;
  int w_, h_;
};

int textWidth(const Font& font, const char* s);

}  // namespace cheerlights

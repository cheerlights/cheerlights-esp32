// Anti-aliased fonts for the screen, generated from Source Sans 3 by tools/make_fonts.py.
#pragma once
#include <stdint.h>

namespace cheerlights {

struct Glyph {
  uint32_t offset;  // into the font's alpha data, in bytes
  uint8_t width, height;
  int8_t left;  // from the pen to the glyph's left edge
  int8_t top;   // from the baseline up to the glyph's top edge
  uint8_t advance;
};

// Each pixel is a coverage from 0 to 15, two pixels to a byte, high nibble first, row by row.
struct Font {
  const uint8_t* alpha;
  const Glyph* glyphs;
  uint8_t first, last;
  uint8_t ascent, descent;
};

extern const Font FONT_NAME;   // Source Sans 3 Bold, 46 px: the color's name
extern const Font FONT_TITLE;  // Source Sans 3 Bold, 24 px: headings on the setup screens
extern const Font FONT_TEXT;   // Source Sans 3 Regular, 18 px: everything else

}  // namespace cheerlights

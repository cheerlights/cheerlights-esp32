// Scrolling text: the owner's own words, scrolling over the color or the animation on a grid of LEDs.
// No hardware dependencies, so the native tests cover all of it.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "profile.h"

namespace cheerlights {

// The longest message, in characters once it is turned into the characters the font has.
constexpr size_t TEXT_MAX = 64;

// The font's rows. A grid needs at least this many rows and columns to show text.
constexpr int TEXT_ROWS = 8;

// What the owner set: the message, whether it scrolls, its color, and how fast.
struct ScrollText {
  char message[TEXT_MAX + 1] = "";
  bool on = false;
  // Auto picks white on a dark color and dark (unlit) on a light one, so the words stay readable on any color.
  bool autoColor = true;
  Rgb8 color{0xff, 0xff, 0xff};  // when not auto; 000000 is dark, the LEDs off where the letters are
  uint8_t speed = 128;           // 1 to 255, slow to fast
  // Quarter turns of the letters, clockwise, 0 to 3, so a grid worn or mounted any way round reads upright.
  uint8_t turn = 0;
};

bool operator==(const ScrollText& a, const ScrollText& b);
inline bool operator!=(const ScrollText& a, const ScrollText& b) { return !(a == b); }

// Turns UTF-8 text into the characters the font has: printable ASCII as it is, curly quotes, dashes, the ellipsis,
// and the no-break space as their plain forms, and accented Latin letters without their accents, since phone
// keyboards type these by themselves. Returns nullptr when it fits in TEXT_MAX, or why not.
const char* normalizeText(const char* utf8, char out[TEXT_MAX + 1]);

// The message laid out as columns of pixels, bit 0 the top row: each letter as wide as it is, one column apart.
struct TextStrip {
  uint8_t columns[TEXT_MAX * 6];
  uint16_t width = 0;
};
void layoutText(const char* message, TextStrip& strip);

// Whether a pixel at column x and row y of a width by height grid is part of a letter, elapsedMs after the message
// started at the right edge. The message scrolls left, leaves, and after one grid's width of plain color comes in
// again. A grid of 16 rows or more shows the letters at twice the size, and so on; they sit in the middle rows.
// turn is in quarter turns clockwise: at 1 the letters' tops face the grid's right edge (x = width - 1).
bool textLit(const TextStrip& strip, uint8_t speed, uint32_t elapsedMs, int x, int y, int width, int height,
             uint8_t turn = 0);

// Columns a second at a speed, 4 at the slowest and 24 at the fastest.
uint32_t textColumnsPerSecond(uint8_t speed);

// The light the letters are sent, as an LED value. background is the LED color the grid shows behind them; an
// animation that plays its own colors passes ownColors, and Auto then draws dark letters, which read on any of them.
Rgb8 textInk(const ScrollText& text, Rgb8 background, bool ownColors);

// How light a color looks on LEDs, 0 to 1000. An LED's light grows in a straight line with its value.
int ledLightness(Rgb8 led);

// Auto draws dark letters from this lightness up, and white ones below it.
constexpr int TEXT_DARK_FROM = 500;

// The text kept in flash: a version, flags, speed, color, length, and the message.
constexpr size_t TEXT_STORED_SIZE = 7 + TEXT_MAX;
void encodeText(const ScrollText& text, uint8_t out[TEXT_STORED_SIZE]);
bool decodeText(const uint8_t* data, size_t length, ScrollText& out);

// The Text characteristic (docs/ble-profile.md): a version, flags, speed, the letters' color, a refusal, the
// message's length in bytes, and the message. A phone writes the words as typed, in UTF-8; the light keeps them the
// way its font has them, and a read returns what it keeps, with the reason it turned down the last write, if it did.
constexpr uint8_t TEXT_VALUE_VERSION = 1;
constexpr size_t TEXT_VALUE_HEADER = 8;                          // the fields before the message
constexpr size_t TEXT_VALUE_SIZE = TEXT_VALUE_HEADER + TEXT_MAX;  // the longest value a light sends
enum class TextRefusal : uint8_t {
  None = 0,
  Character = 1,  // a character the font does not have, even without its accent
  Length = 2,     // longer than TEXT_MAX characters once in the font's characters
};
// Reads a phone's write. Ok gives the new text in out; a refusal leaves out as current and says why in refusal.
// An empty message turns the text off. WrongLength and UnknownVersion are ignored, like any other value.
ParseResult parseTextValue(const uint8_t* data, size_t length, const ScrollText& current, ScrollText& out,
                           TextRefusal& refusal);
// The value a read returns. Returns its length.
size_t encodeTextValue(const ScrollText& text, TextRefusal refusal, uint8_t out[TEXT_VALUE_SIZE]);

// The text command on the USB console. NotText: the line is some other command. None: nothing changed (a look, or
// a mistake). Changed: the caller keeps and shows the new text. scrolls is false on a light that cannot show text,
// which then says so instead of changing anything.
enum class TextAction { NotText, None, Changed };
TextAction handleTextCommand(const char* line, bool scrolls, ScrollText& text, char* out, size_t size);

// A line describing the text, as the console shows it: text on "Hello" color auto speed 128 turn 0.
void describeText(const ScrollText& text, char* out, size_t size);

}  // namespace cheerlights

#include "text.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cheerlights {

namespace {

// Printable ASCII, from the classic 5x7 font of the Adafruit GFX Library, five columns a letter, bit 0 the top row.
// Row 7 holds the tails of g, j, p, q, y, and the comma. The apostrophe is redrawn as a plain tick.
//
// Copyright (c) 2012 Adafruit Industries. All rights reserved.
// Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
// following conditions are met:
// - Redistributions of source code must retain the above copyright notice, this list of conditions and the following
//   disclaimer.
// - Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
//   following disclaimer in the documentation and/or other materials provided with the distribution.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
const uint8_t FONT[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x00, 0x00, 0x5f, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7f, 0x14, 0x7f, 0x14},  // #
    {0x24, 0x2a, 0x7f, 0x2a, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x56, 0x20, 0x50},  // &
    {0x00, 0x00, 0x07, 0x00, 0x00},  // apostrophe
    {0x00, 0x1c, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1c, 0x00},  // )
    {0x2a, 0x1c, 0x7f, 0x1c, 0x2a},  // *
    {0x08, 0x08, 0x3e, 0x08, 0x08},  // +
    {0x00, 0x80, 0x70, 0x30, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x00, 0x60, 0x60, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3e, 0x51, 0x49, 0x45, 0x3e},  // 0
    {0x00, 0x42, 0x7f, 0x40, 0x00},  // 1
    {0x72, 0x49, 0x49, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x49, 0x4d, 0x33},  // 3
    {0x18, 0x14, 0x12, 0x7f, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3c, 0x4a, 0x49, 0x49, 0x31},  // 6
    {0x41, 0x21, 0x11, 0x09, 0x07},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x46, 0x49, 0x49, 0x29, 0x1e},  // 9
    {0x00, 0x00, 0x14, 0x00, 0x00},  // :
    {0x00, 0x40, 0x34, 0x00, 0x00},  // ;
    {0x00, 0x08, 0x14, 0x22, 0x41},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x00, 0x41, 0x22, 0x14, 0x08},  // >
    {0x02, 0x01, 0x59, 0x09, 0x06},  // ?
    {0x3e, 0x41, 0x5d, 0x59, 0x4e},  // @
    {0x7c, 0x12, 0x11, 0x12, 0x7c},  // A
    {0x7f, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3e, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7f, 0x41, 0x41, 0x41, 0x3e},  // D
    {0x7f, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7f, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3e, 0x41, 0x41, 0x51, 0x73},  // G
    {0x7f, 0x08, 0x08, 0x08, 0x7f},  // H
    {0x00, 0x41, 0x7f, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3f, 0x01},  // J
    {0x7f, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7f, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7f, 0x02, 0x1c, 0x02, 0x7f},  // M
    {0x7f, 0x04, 0x08, 0x10, 0x7f},  // N
    {0x3e, 0x41, 0x41, 0x41, 0x3e},  // O
    {0x7f, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3e, 0x41, 0x51, 0x21, 0x5e},  // Q
    {0x7f, 0x09, 0x19, 0x29, 0x46},  // R
    {0x26, 0x49, 0x49, 0x49, 0x32},  // S
    {0x03, 0x01, 0x7f, 0x01, 0x03},  // T
    {0x3f, 0x40, 0x40, 0x40, 0x3f},  // U
    {0x1f, 0x20, 0x40, 0x20, 0x1f},  // V
    {0x3f, 0x40, 0x38, 0x40, 0x3f},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x03, 0x04, 0x78, 0x04, 0x03},  // Y
    {0x61, 0x59, 0x49, 0x4d, 0x43},  // Z
    {0x00, 0x7f, 0x41, 0x41, 0x41},  // [
    {0x02, 0x04, 0x08, 0x10, 0x20},  // backslash
    {0x00, 0x41, 0x41, 0x41, 0x7f},  // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},  // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},  // _
    {0x00, 0x03, 0x07, 0x08, 0x00},  // `
    {0x20, 0x54, 0x54, 0x78, 0x40},  // a
    {0x7f, 0x28, 0x44, 0x44, 0x38},  // b
    {0x38, 0x44, 0x44, 0x44, 0x28},  // c
    {0x38, 0x44, 0x44, 0x28, 0x7f},  // d
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x00, 0x08, 0x7e, 0x09, 0x02},  // f
    {0x18, 0xa4, 0xa4, 0x9c, 0x78},  // g
    {0x7f, 0x08, 0x04, 0x04, 0x78},  // h
    {0x00, 0x44, 0x7d, 0x40, 0x00},  // i
    {0x20, 0x40, 0x40, 0x3d, 0x00},  // j
    {0x7f, 0x10, 0x28, 0x44, 0x00},  // k
    {0x00, 0x41, 0x7f, 0x40, 0x00},  // l
    {0x7c, 0x04, 0x78, 0x04, 0x78},  // m
    {0x7c, 0x08, 0x04, 0x04, 0x78},  // n
    {0x38, 0x44, 0x44, 0x44, 0x38},  // o
    {0xfc, 0x18, 0x24, 0x24, 0x18},  // p
    {0x18, 0x24, 0x24, 0x18, 0xfc},  // q
    {0x7c, 0x08, 0x04, 0x04, 0x08},  // r
    {0x48, 0x54, 0x54, 0x54, 0x24},  // s
    {0x04, 0x04, 0x3f, 0x44, 0x24},  // t
    {0x3c, 0x40, 0x40, 0x20, 0x7c},  // u
    {0x1c, 0x20, 0x40, 0x20, 0x1c},  // v
    {0x3c, 0x40, 0x30, 0x40, 0x3c},  // w
    {0x44, 0x28, 0x10, 0x28, 0x44},  // x
    {0x4c, 0x90, 0x90, 0x90, 0x7c},  // y
    {0x44, 0x64, 0x54, 0x4c, 0x44},  // z
    {0x00, 0x08, 0x36, 0x41, 0x00},  // {
    {0x00, 0x00, 0x77, 0x00, 0x00},  // |
    {0x00, 0x41, 0x36, 0x08, 0x00},  // }
    {0x02, 0x01, 0x02, 0x04, 0x02},  // ~
};

constexpr int SPACE_WIDTH = 2;  // a space is two blank columns, plus the one after every letter

bool printable(char c) { return c >= 0x20 && c <= 0x7e; }

// The plain form of a code point, or nullptr when the font cannot show it.
const char* plainForm(uint32_t cp) {
  static char one[2];
  if (cp >= 0x20 && cp <= 0x7e) {
    one[0] = static_cast<char>(cp);
    one[1] = '\0';
    return one;
  }
  switch (cp) {
    case '\t': case '\n': case '\r': case 0xa0: return " ";
    case 0x2018: case 0x2019: case 0x201a: case 0x2032: case 0xb4: return "'";
    case 0x201c: case 0x201d: case 0x201e: case 0x2033: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2026: return "...";
    case 0xc6: return "AE";
    case 0xe6: return "ae";
    case 0xdf: return "ss";
    case 0xd7: return "x";
    default: break;
  }
  // Accented Latin letters, U+00C0 to U+00FF, as their letters; a dot is a character with no plain form.
  static const char LATIN[] = "AAAAAA.CEEEEIIIIDNOOOOO.OUUUUY..aaaaaa.ceeeeiiiidnooooo.ouuuuy.y";
  if (cp >= 0xc0 && cp <= 0xff && LATIN[cp - 0xc0] != '.') {
    one[0] = LATIN[cp - 0xc0];
    one[1] = '\0';
    return one;
  }
  return nullptr;
}

// Reads one UTF-8 code point and moves p past it; a malformed byte reads as 0xFFFD.
uint32_t nextCodePoint(const unsigned char*& p) {
  const unsigned char c = *p++;
  if (c < 0x80) return c;
  int more = 0;
  uint32_t cp = 0;
  if ((c & 0xe0) == 0xc0) {
    more = 1;
    cp = c & 0x1f;
  } else if ((c & 0xf0) == 0xe0) {
    more = 2;
    cp = c & 0x0f;
  } else if ((c & 0xf8) == 0xf0) {
    more = 3;
    cp = c & 0x07;
  } else {
    return 0xfffd;
  }
  for (int i = 0; i < more; i++) {
    if ((*p & 0xc0) != 0x80) return 0xfffd;
    cp = (cp << 6) | (*p++ & 0x3f);
  }
  return cp;
}

void ledOf(Rgb8 color, Rgb8& out) {
  const uint8_t index = paletteIndexOf(color);
  out = index < PALETTE_SIZE ? LED_PALETTE[index] : ledColor(color);
}

}  // namespace

bool operator==(const ScrollText& a, const ScrollText& b) {
  return strcmp(a.message, b.message) == 0 && a.on == b.on && a.autoColor == b.autoColor && a.color == b.color &&
         a.speed == b.speed && a.turn == b.turn;
}

const char* normalizeText(const char* utf8, char out[TEXT_MAX + 1]) {
  char work[TEXT_MAX + 1];
  size_t n = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8 != nullptr ? utf8 : "");
  while (*p) {
    const char* plain = plainForm(nextCodePoint(p));
    if (plain == nullptr) {
      return "The text has a character this light cannot show. It shows letters, numbers, spaces, and keyboard "
             "punctuation.";
    }
    for (const char* q = plain; *q; q++) {
      if (n == TEXT_MAX) return "The text is longer than 64 characters.";
      work[n++] = *q;
    }
  }
  // Spaces at either end would only add blank columns.
  size_t start = 0;
  while (start < n && work[start] == ' ') start++;
  while (n > start && work[n - 1] == ' ') n--;
  memcpy(out, work + start, n - start);
  out[n - start] = '\0';
  return nullptr;
}

void layoutText(const char* message, TextStrip& strip) {
  strip.width = 0;
  for (const char* c = message; *c && static_cast<size_t>(strip.width) + 6 <= sizeof(strip.columns); c++) {
    if (!printable(*c)) continue;
    if (*c == ' ') {
      for (int i = 0; i < SPACE_WIDTH + 1; i++) strip.columns[strip.width++] = 0;
      continue;
    }
    // Each letter as wide as its ink, so an i or a period does not take the room of an m.
    const uint8_t* g = FONT[*c - 0x20];
    int first = 0, last = 4;
    while (first < 4 && g[first] == 0) first++;
    while (last > first && g[last] == 0) last--;
    for (int i = first; i <= last; i++) strip.columns[strip.width++] = g[i];
    strip.columns[strip.width++] = 0;
  }
  if (strip.width > 0) strip.width--;  // no gap after the last letter
}

uint32_t textColumnsPerSecond(uint8_t speed) {
  if (speed == 0) speed = 1;
  return 4u + (static_cast<uint32_t>(speed - 1) * 20u + 127u) / 254u;
}

bool textLit(const TextStrip& strip, uint8_t speed, uint32_t elapsedMs, int x, int y, int width, int height,
             uint8_t turn) {
  // Into the letters' own frame: a quarter turn swaps the grid's width and height.
  if ((turn & 3) == 1) {
    const int tx = y, ty = width - 1 - x;
    x = tx;
    y = ty;
    const int w = width;
    width = height;
    height = w;
  } else if ((turn & 3) == 2) {
    x = width - 1 - x;
    y = height - 1 - y;
  } else if ((turn & 3) == 3) {
    const int tx = height - 1 - y, ty = x;
    x = tx;
    y = ty;
    const int w = width;
    width = height;
    height = w;
  }
  if (strip.width == 0 || height < TEXT_ROWS || width < 1) return false;
  const int scale = height / TEXT_ROWS;
  const int top = (height - TEXT_ROWS * scale) / 2;
  if (y < top || y >= top + TEXT_ROWS * scale) return false;
  const int row = (y - top) / scale;
  const int columns = (width + scale - 1) / scale;  // the grid's width in letter columns
  // Comes in at the right edge, goes out at the left, then one grid's width of plain color before it comes again.
  const uint32_t cycle = static_cast<uint32_t>(columns) * 2u + strip.width;
  const uint32_t moved = static_cast<uint32_t>(static_cast<uint64_t>(elapsedMs) * textColumnsPerSecond(speed) / 1000u);
  const int at = static_cast<int>(moved % cycle) + x / scale - columns;
  if (at < 0 || at >= strip.width) return false;
  return (strip.columns[at] >> row) & 1;
}

int ledLightness(Rgb8 led) { return (2126 * led.r + 7152 * led.g + 722 * led.b) / 2550; }

Rgb8 textInk(const ScrollText& text, Rgb8 background, bool ownColors) {
  if (!text.autoColor) {
    Rgb8 led;
    ledOf(text.color, led);
    return led;
  }
  if (ownColors || ledLightness(background) >= TEXT_DARK_FROM) return {0, 0, 0};
  return {0xff, 0xff, 0xff};
}

namespace {
constexpr uint8_t TEXT_STORED_VERSION = 1;
}

void encodeText(const ScrollText& text, uint8_t out[TEXT_STORED_SIZE]) {
  memset(out, 0, TEXT_STORED_SIZE);
  out[0] = TEXT_STORED_VERSION;
  // Bits 2 and 3 are the turn, zero in text saved before it existed.
  out[1] = static_cast<uint8_t>((text.on ? 1 : 0) | (text.autoColor ? 2 : 0) | ((text.turn & 3) << 2));
  out[2] = text.speed;
  out[3] = text.color.r;
  out[4] = text.color.g;
  out[5] = text.color.b;
  const size_t length = strnlen(text.message, TEXT_MAX);
  out[6] = static_cast<uint8_t>(length);
  memcpy(out + 7, text.message, length);
}

bool decodeText(const uint8_t* data, size_t length, ScrollText& out) {
  if (data == nullptr || length != TEXT_STORED_SIZE || data[0] != TEXT_STORED_VERSION) return false;
  if (data[6] > TEXT_MAX || data[2] == 0) return false;
  ScrollText t;
  for (size_t i = 0; i < data[6]; i++) {
    const char c = static_cast<char>(data[7 + i]);
    if (!printable(c)) return false;
    t.message[i] = c;
  }
  t.message[data[6]] = '\0';
  t.on = (data[1] & 1) != 0;
  t.autoColor = (data[1] & 2) != 0;
  t.turn = (data[1] >> 2) & 3;
  t.speed = data[2];
  t.color = {data[3], data[4], data[5]};
  out = t;
  return true;
}

namespace {

void colorName(const ScrollText& text, char out[8]) {
  if (text.autoColor) {
    snprintf(out, 8, "auto");
  } else if (text.color == Rgb8{0xff, 0xff, 0xff}) {
    snprintf(out, 8, "white");
  } else if (text.color == Rgb8{0, 0, 0}) {
    snprintf(out, 8, "dark");
  } else {
    snprintf(out, 8, "%02x%02x%02x", text.color.r, text.color.g, text.color.b);
  }
}

bool sameWord(const char* a, size_t length, const char* word) {
  if (strlen(word) != length) return false;
  for (size_t i = 0; i < length; i++) {
    if (tolower(static_cast<unsigned char>(a[i])) != word[i]) return false;
  }
  return true;
}

const char TEXT_HELP[] =
    "Usage: text \"<words>\" | text on | text off | text clear | text color <auto|white|dark|rrggbb> | "
    "text speed <1-255> | text turn <0|90|180|270>";

}  // namespace

void describeText(const ScrollText& text, char* out, size_t size) {
  char color[8];
  colorName(text, color);
  snprintf(out, size, "text %s \"%s\" color %s speed %u turn %u", text.on ? "on" : "off", text.message, color,
           text.speed, text.turn * 90u);
}

TextAction handleTextCommand(const char* line, bool scrolls, ScrollText& text, char* out, size_t size) {
  out[0] = '\0';
  const char* p = line;
  while (*p == ' ') p++;
  if (!sameWord(p, strnlen(p, 4) == 4 ? 4 : 0, "text") || (p[4] != '\0' && p[4] != ' ')) return TextAction::NotText;
  p += 4;
  while (*p == ' ') p++;
  if (!scrolls) {
    snprintf(out, size, "This light cannot show text. It needs a grid of at least %d by %d LEDs.", TEXT_ROWS, TEXT_ROWS);
    return TextAction::None;
  }
  if (*p == '\0') {
    describeText(text, out, size);
    return TextAction::None;
  }
  // The first word, and what follows it.
  const char* word = p;
  size_t wordLength = 0;
  while (word[wordLength] && word[wordLength] != ' ') wordLength++;
  const char* rest = word + wordLength;
  while (*rest == ' ') rest++;
  const bool alone = *rest == '\0';

  ScrollText next = text;
  if (*p != '"' && alone && sameWord(word, wordLength, "on")) {
    if (text.message[0] == '\0') {
      snprintf(out, size, "There is no text yet. Type text \"your words\" to set it.");
      return TextAction::None;
    }
    next.on = true;
    snprintf(out, size, "Text on.");
  } else if (*p != '"' && alone && sameWord(word, wordLength, "off")) {
    next.on = false;
    snprintf(out, size, "Text off. Type text on to show the words again.");
  } else if (*p != '"' && alone && sameWord(word, wordLength, "clear")) {
    next.message[0] = '\0';
    next.on = false;
    snprintf(out, size, "Text cleared.");
  } else if (*p != '"' && sameWord(word, wordLength, "color") && !alone) {
    char value[8] = "";
    size_t n = 0;
    for (const char* q = rest; *q && *q != ' ' && n < sizeof(value) - 1; q++) {
      value[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*q)));
    }
    value[n] = '\0';
    const char* hex = value[0] == '#' ? value + 1 : value;
    if (strcmp(value, "auto") == 0) {
      next.autoColor = true;
    } else if (strcmp(value, "white") == 0) {
      next.autoColor = false;
      next.color = {0xff, 0xff, 0xff};
    } else if (strcmp(value, "dark") == 0) {
      next.autoColor = false;
      next.color = {0, 0, 0};
    } else if (strlen(hex) == 6 && strspn(hex, "0123456789abcdef") == 6) {
      const unsigned long v = strtoul(hex, nullptr, 16);
      next.autoColor = false;
      next.color = {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
    } else {
      snprintf(out, size, "The text color is auto, white, dark, or six hex digits, such as ff0000 for red.");
      return TextAction::None;
    }
    char name[8];
    colorName(next, name);
    snprintf(out, size, "Text color %s.", name);
  } else if (*p != '"' && sameWord(word, wordLength, "speed") && !alone) {
    char* end = nullptr;
    const long v = strtol(rest, &end, 10);
    if (end == rest || *end != '\0' || v < 1 || v > 255) {
      snprintf(out, size, "The text speed is a whole number, 1 to 255.");
      return TextAction::None;
    }
    next.speed = static_cast<uint8_t>(v);
    snprintf(out, size, "Text speed %ld.", v);
  } else if (*p != '"' && sameWord(word, wordLength, "turn") && !alone) {
    const int degrees = atoi(rest);
    if (strspn(rest, "0123456789") != strlen(rest) ||
        (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)) {
      snprintf(out, size, "The turn is 0, 90, 180, or 270 degrees, clockwise.");
      return TextAction::None;
    }
    next.turn = static_cast<uint8_t>(degrees / 90);
    snprintf(out, size, "Text turned %d degrees.", degrees);
  } else if (*p != '"' && alone && (sameWord(word, wordLength, "color") || sameWord(word, wordLength, "speed") ||
                                    sameWord(word, wordLength, "turn"))) {
    snprintf(out, size, "%s", TEXT_HELP);
    return TextAction::None;
  } else {
    // The words: everything between the first and last quote, or the rest of the line without quotes.
    char words[160];
    if (*p == '"') {
      const char* close = strrchr(p + 1, '"');
      if (close == nullptr) {
        snprintf(out, size, "%s", TEXT_HELP);
        return TextAction::None;
      }
      const size_t n = static_cast<size_t>(close - (p + 1));
      snprintf(words, sizeof(words), "%.*s", static_cast<int>(n < sizeof(words) - 1 ? n : sizeof(words) - 1), p + 1);
    } else {
      snprintf(words, sizeof(words), "%s", p);
    }
    const char* why = normalizeText(words, next.message);
    if (why != nullptr) {
      snprintf(out, size, "%s", why);
      return TextAction::None;
    }
    if (next.message[0] == '\0') {
      snprintf(out, size, "There are no words to show. To remove the text, type text clear.");
      return TextAction::None;
    }
    next.on = true;
    snprintf(out, size, "Text \"%s\" scrolls now.", next.message);
  }
  if (next == text) return TextAction::None;
  text = next;
  return TextAction::Changed;
}

}  // namespace cheerlights

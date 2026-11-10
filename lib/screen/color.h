// The CheerLights palette and the colors drawn on top of it.
#pragma once
#include <stddef.h>

#include "canvas.h"

namespace cheerlights {

// The palette itself (PALETTE, from docs/colors.json) is in profile.h, shared with the LEDs.

// Reads "#rrggbb" or "rrggbb", as the CheerLights MQTT topic cheerlightsRGB sends it, ignoring spaces around it.
bool parseHex(const char* text, size_t length, Rgb& out);

// The palette name of an exact palette color, or nullptr.
const char* paletteName(Rgb c);

// The palette name, or "#rrggbb" for a color outside the palette.
void colorLabel(Rgb c, char out[8]);

// Whether white text reads better on this color than black, by WCAG contrast.
// It gives the "text" column of docs/colors.json for every palette color.
bool whiteInkFor(Rgb c);

// Text on the color: white or near-black.
Rgb inkFor(Rgb background);

// WCAG relative luminance, 0 for black to 1 for white.
double luminance(Rgb c);

// Snow and twinkles: white wherever white shows, and a cool gray over the light colors (white, oldlace, yellow,
// cyan, pink), where white would vanish.
Rgb accentFor(Rgb background);

}  // namespace cheerlights

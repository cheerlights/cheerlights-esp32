#include "screen.h"

#include <stdio.h>
#include <string.h>

#include "color.h"

namespace cheerlights {

namespace {

// The install page's dark ground and ink, so the setup screens look like the rest of CheerLights.
const Rgb GROUND{0x14, 0x15, 0x17};
const Rgb INK{0xe9, 0xea, 0xe6};
const Rgb MUTED{0xa9, 0xad, 0xa6};
constexpr int MARGIN = 14;

// A title and up to three lines, centered top to bottom.
void card(Canvas& c, const char* title, const char* lines[], int n, const bool strong[]) {
  c.fill(GROUND);
  constexpr int TITLE_CAP = 17, GAP = 32, LINE = 23;
  const int block = TITLE_CAP + GAP + (n - 1) * LINE;
  const int top = (c.height() - block) / 2 + TITLE_CAP;
  c.text(FONT_TITLE, MARGIN, top, title, INK);
  char fitted[64];
  for (int i = 0; i < n; i++) {
    if (lines[i] == nullptr) continue;
    fitText(FONT_TEXT, lines[i], c.width() - 2 * MARGIN, fitted, sizeof(fitted));
    c.text(FONT_TEXT, MARGIN, top + GAP + i * LINE, fitted, strong[i] ? INK : MUTED);
  }
}

void infoCard(Canvas& c, const View& v) {
  c.fill(GROUND);
  c.text(FONT_TITLE, MARGIN, 30, "CheerLights", INK);
  const int valueX = 92;
  for (int i = 0; i < 5; i++) {
    const char* line = v.infoLines[i];
    if (line == nullptr) continue;
    const char* tab = strchr(line, '\t');
    char label[24], value[64];
    if (tab != nullptr) {
      snprintf(label, sizeof(label), "%.*s", static_cast<int>(tab - line), line);
      fitText(FONT_TEXT, tab + 1, c.width() - valueX - MARGIN / 2, value, sizeof(value));
    } else {
      label[0] = '\0';
      fitText(FONT_TEXT, line, c.width() - 2 * MARGIN, value, sizeof(value));
    }
    const int y = 56 + i * 19;
    c.text(FONT_TEXT, MARGIN, y, label, MUTED);
    c.text(FONT_TEXT, tab != nullptr ? valueX : MARGIN, y, value, INK);
  }
}

}  // namespace

void fitText(const Font& font, const char* s, int width, char* out, size_t size) {
  snprintf(out, size, "%s", s);
  if (textWidth(font, out) <= width) return;
  for (size_t n = strlen(out); n > 0; n--) {
    snprintf(out, size, "%.*s...", static_cast<int>(n - 1), s);
    if (textWidth(font, out) <= width) return;
  }
  snprintf(out, size, "...");
}

void drawView(Canvas& c, const View& v, const SceneState& scene) {
  if (v.info) {
    infoCard(c, v);
    return;
  }
  switch (v.status) {
    case Status::NoWifi: {
      const char* lines[] = {"Set up Wi-Fi at", v.setupUrl};
      const bool strong[] = {false, true};
      card(c, "CheerLights", lines, 2, strong);
      return;
    }
    case Status::Scanning: {
      char saved[40];
      snprintf(saved, sizeof(saved), "%d saved network%s", v.saved, v.saved == 1 ? "" : "s");
      const char* lines[] = {"Looking for Wi-Fi", saved};
      const bool strong[] = {false, true};
      card(c, "CheerLights", lines, 2, strong);
      return;
    }
    case Status::Joining: {
      const char* lines[] = {"Joining Wi-Fi", v.ssid};
      const bool strong[] = {false, true};
      card(c, "CheerLights", lines, 2, strong);
      return;
    }
    case Status::NoneInRange: {
      const char* lines[] = {"Still looking. To add one:", v.setupUrl};
      const bool strong[] = {false, true};
      card(c, "No saved Wi-Fi here", lines, 2, strong);
      return;
    }
    case Status::JoinFailed: {
      const char* lines[] = {v.ssid, "Still trying. To change it:", v.setupUrl};
      const bool strong[] = {true, false, true};
      card(c, "Can't join Wi-Fi", lines, 3, strong);
      return;
    }
    case Status::Connecting: {
      const char* lines[] = {"On Wi-Fi. Waiting for", "the CheerLights color."};
      const bool strong[] = {false, false};
      card(c, "CheerLights", lines, 2, strong);
      return;
    }
    case Status::Waiting: {
      const char* lines[] = {"Add it in the app as", v.ssid};
      const bool strong[] = {false, true};
      card(c, "CheerLights", lines, 2, strong);
      return;
    }
    case Status::Showing:
      break;
  }
  const Rgb background = mix(v.from, v.color, v.fade);
  sceneDraw(scene, c, background);
  // The old name fades out over the first half of the change, and the new one fades in over the second.
  const bool second = v.fade >= 0.5f;
  const Rgb named = second ? v.color : v.from;
  char label[8];
  colorLabel(named, label);
  const float strength = second ? v.fade * 2 - 1 : 1 - v.fade * 2;
  if (!v.hideLabel) c.text(FONT_NAME, MARGIN, c.height() - 20, label, mix(background, inkFor(named), strength));
  if (v.offline) c.text(FONT_TEXT, MARGIN, 26, "Offline, reconnecting", inkFor(background));
}

}  // namespace cheerlights

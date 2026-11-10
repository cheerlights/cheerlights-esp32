// Everything the screen shows: the setup and connection steps, the color with its animation, and the info card.
#pragma once
#include <stddef.h>

#include "canvas.h"
#include "scenes.h"

namespace cheerlights {

enum class Status : uint8_t {
  NoWifi,       // no network saved: points to the setup page
  Scanning,     // looking for a saved network in range
  Joining,      // joining one
  JoinFailed,   // a saved network is in range but refused or did not answer; it keeps trying
  NoneInRange,  // none of the saved networks is in range; it keeps looking
  Connecting,   // on Wi-Fi, waiting for the first color from CheerLights
  Showing,      // showing the color
  Waiting,      // set to Bluetooth, waiting for a phone to send the first color
};

struct View {
  Status status;
  const char* ssid;  // the network being joined, or the last one tried; for Waiting, the light's Bluetooth name
  int saved;         // how many networks are saved
  const char* setupUrl;
  Rgb color;    // the color now
  Rgb from;     // the color before it, while it fades in
  float fade;   // 0 shows from, 1 shows color
  bool offline;  // showing the last color while Wi-Fi or CheerLights is lost
  bool hideLabel;  // the owner turned the color's name off
  bool info;     // the info card, after the left button
  const char* infoLines[5];  // label and value separated by a tab
};

void drawView(Canvas& canvas, const View& view, const SceneState& scene);

// Copies s into out, cut short with "..." so it fits in width pixels.
void fitText(const Font& font, const char* s, int width, char* out, size_t size);

}  // namespace cheerlights

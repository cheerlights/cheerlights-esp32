// What the parts of the firmware share. main.cpp owns the settings, the light, and the console; ble_link.cpp and
// wifi_link.cpp bring the color in, one or the other by the settings' link; screen_out.cpp draws on the T-Display.
#pragma once
#include <device.h>
#include <profile.h>
#include <stddef.h>
#include <stdint.h>

#include "api.h"
#include "screen.h"

namespace cheerlights {

constexpr uint8_t FW_MAJOR = 0, FW_MINOR = 5, FW_PATCH = 0;
extern const char FW_TEXT[];

// The setup page, short enough to read off a screen.
constexpr const char* SETUP_URL = "cheerlights.com/setup";

extern Settings settings;  // what this boot runs with
extern Scene scene;        // what the light shows: the color, off, and the light (brightness, effect, speed)
extern char deviceName[9];  // CL- and four hex digits, from the chip's MAC address
extern char hostName[17];   // cheerlights- and the same four hex digits, for the network: cheerlights-8f00.local

void logf(const char* format, ...);
void logText(const char* text);

// A new color to show, from the phone or from CheerLights.
void showFrame(const Frame& frame, bool haveColor);
// The light changed from the phone or the network: show it, and keep it in flash after a moment.
void lightChanged(const Light& light);
// The animations this light has.
const EffectInfo* lightEffects(size_t& count);
// Blink white for IDENTIFY_MS, then go back to the color. identifying() says whether it is blinking and, if so,
// whether this moment is lit.
void identify();
bool identifying(bool& lit);

// Bluetooth (docs/ble-profile.md).
void startBle();
void serviceBle();
void bleLightChanged(const Light& light);  // the button changed the animation, so a phone reads it

// Wi-Fi (docs/wifi.md). Each is a no-op on a chip without Wi-Fi.
void startWifi();
void serviceWifi();
void wifiAbout(char* out, size_t size);  // " wifi=... ip=..." for the about line
void wifiLightChanged(const Light& light);  // the button changed the animation, so the API reports it
void wifiCommand(const char* line, char* out, size_t size, bool& handled);

// The T-Display's screen. Each is a no-op in a build without it.
void startScreen();
void serviceScreen();
// A setup step, with the network or name it names and how many networks are saved.
void screenStatus(Status status, const char* detail, int saved = 0);
void screenInfo();  // the info card, from the left button

}  // namespace cheerlights

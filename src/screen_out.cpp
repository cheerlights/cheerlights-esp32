// The LilyGO T-Display's screen: the color fills it, an animation plays over it, and its name sits at the bottom.
// Before the first color it shows the setup step. Only the ESP32 build has it (CL_SCREEN in platformio.ini).
#include <Arduino.h>

#include "app.h"

#if CL_SCREEN
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>

#include "color.h"
#endif

namespace cheerlights {

#if CL_SCREEN

namespace {

// The original T-Display, from LilyGO's schematic. The panel is 135 by 240, shown in landscape.
constexpr int PIN_SCLK = 18, PIN_MOSI = 19, PIN_CS = 5, PIN_DC = 16, PIN_RST = 23, PIN_BACKLIGHT = 4;
constexpr int WIDTH = 240, HEIGHT = 135;
constexpr uint32_t SPI_HZ = 40000000;  // half duplex, so 40 MHz works through the GPIO matrix on these pins

constexpr uint32_t FRAME_MS = 33;  // about 30 frames a second while an animation runs
constexpr uint32_t FADE_MS = 900;  // a new color fades in over this long
constexpr uint32_t INFO_MS = 6000;

// The ST7789 on its SPI bus. The panel is 135 by 240 inside the controller's 240 by 320, hence the offsets.
class Panel : public lgfx::LGFX_Device {
 public:
  Panel() {
    auto bus = bus_.config();
    bus.spi_host = SPI3_HOST;
    bus.spi_mode = 0;
    bus.freq_write = SPI_HZ;
    bus.freq_read = 6000000;
    bus.dma_channel = SPI_DMA_CH_AUTO;
    bus.pin_sclk = PIN_SCLK;
    bus.pin_mosi = PIN_MOSI;
    bus.pin_miso = -1;
    bus.pin_dc = PIN_DC;
    bus_.config(bus);
    panel_.setBus(&bus_);
    auto panel = panel_.config();
    panel.pin_cs = PIN_CS;
    panel.pin_rst = PIN_RST;
    panel.panel_width = 135;
    panel.panel_height = 240;
    panel.offset_x = 52;
    panel.offset_y = 40;
    panel.invert = true;
    panel.readable = false;
    panel.bus_shared = false;
    panel_.config(panel);
    setPanel(&panel_);
  }

 private:
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ST7789 panel_;
};

Panel tft;
uint16_t* frame = nullptr;
SceneState animation;
bool started = false;

Status status = Status::Waiting;
char detail[33] = "";
int savedNetworks = 0;
bool haveShown = false;
Rgb color{0, 0, 0}, fromColor{0, 0, 0};
uint32_t colorAt = 0;
uint32_t infoUntil = 0;
uint8_t effectShown = 0xFF;
uint8_t backlight = 0;
bool dirty = true;

SceneId sceneFor(uint8_t effect) {
  switch (effect) {
    case EFFECT_SPARKLE: return SceneId::Sparkle;
    case EFFECT_SNOW: return SceneId::Snow;
    case EFFECT_LEAVES: return SceneId::Leaves;
    default: return SceneId::Solid;
  }
}

void draw() {
  View v{};
  v.status = scene.haveColor ? Status::Showing : status;
  v.ssid = detail;
  v.saved = savedNetworks;
  v.setupUrl = SETUP_URL;
  v.color = color;
  v.from = fromColor;
  const uint32_t since = millis() - colorAt;
  v.fade = since >= FADE_MS ? 1.0f : static_cast<float>(since) / FADE_MS;
  v.offline = scene.haveColor && scene.frame.stale;
  v.hideLabel = !settings.label;
  char lines[5][48];
  if (millis() < infoUntil) {
    v.info = true;
    char label[8];
    colorLabel(color, label);
    snprintf(lines[0], sizeof(lines[0]), "Color\t%s", scene.haveColor ? label : "none yet");
    if (settings.link == Link::Wifi) {
      const bool on = WiFi.status() == WL_CONNECTED;
      snprintf(lines[1], sizeof(lines[1]), "Wi-Fi\t%s", on ? WiFi.SSID().c_str() : "not connected");
      snprintf(lines[2], sizeof(lines[2]), "Address\t%s", on ? WiFi.localIP().toString().c_str() : "none");
      snprintf(lines[3], sizeof(lines[3]), "Name\t%s.local", hostName);
    } else {
      snprintf(lines[1], sizeof(lines[1]), "Link\tBluetooth");
      snprintf(lines[2], sizeof(lines[2]), "Name\t%s", deviceName);
      snprintf(lines[3], sizeof(lines[3]), "Setup\t%s", SETUP_URL);
    }
    snprintf(lines[4], sizeof(lines[4]), "Firmware\t%s", FW_TEXT);
    for (int i = 0; i < 5; i++) v.infoLines[i] = lines[i];
  }
  Canvas canvas(frame, WIDTH, HEIGHT);
  drawView(canvas, v, animation);
  // The frame is already in the screen's byte order, so DMA sends it as it is.
  tft.startWrite();
  tft.pushImageDMA(0, 0, WIDTH, HEIGHT, reinterpret_cast<const lgfx::swap565_t*>(frame));
  tft.endWrite();  // waits for the transfer, so the next frame can draw into the same memory
}

}  // namespace

void startScreen() {
  ledcAttach(PIN_BACKLIGHT, 5000, 8);
  ledcWrite(PIN_BACKLIGHT, 0);  // dark until the first frame is on the panel
  tft.init();
  tft.setRotation(settings.flip ? 3 : 1);
  frame = static_cast<uint16_t*>(malloc(WIDTH * HEIGHT * sizeof(uint16_t)));
  if (frame == nullptr) {
    logf("screen failed reason=memory\n");
    return;
  }
  started = true;
  effectShown = scene.light.effect;
  sceneStart(animation, sceneFor(effectShown), WIDTH, HEIGHT, esp_random());
  draw();
  logf("screen ready\n");
}

void serviceScreen() {
  if (!started) return;
  const uint32_t now = millis();
  // The backlight is the brightness; the screen has no off.
  const uint8_t level = capBrightness(scene.light.brightness, settings.maxBrightness);
  if (level != backlight) {
    backlight = level;
    ledcWrite(PIN_BACKLIGHT, level);
  }
  if (scene.light.effect != effectShown) {
    effectShown = scene.light.effect;
    sceneStart(animation, sceneFor(effectShown), WIDTH, HEIGHT, esp_random());
    infoUntil = 0;
    dirty = true;
  }
  const Rgb next{scene.frame.r, scene.frame.g, scene.frame.b};
  if (scene.haveColor && (!haveShown || next != color)) {
    // Fade from whatever is on the screen now, even halfway through the last fade.
    const uint32_t since = now - colorAt;
    fromColor = haveShown ? mix(fromColor, color, since >= FADE_MS ? 1.0f : static_cast<float>(since) / FADE_MS) : next;
    color = next;
    colorAt = now;
    haveShown = true;
    dirty = true;
  }
  static bool staleShown = false;
  if (scene.frame.stale != staleShown) {
    staleShown = scene.frame.stale;
    dirty = true;
  }
  if (infoUntil != 0 && now >= infoUntil) {
    infoUntil = 0;
    dirty = true;
  }
  // A blink fills the screen white and black, then the picture comes back.
  static bool wasBlinking = false;
  bool lit = false;
  if (identifying(lit)) {
    static int lastShown = -1;
    if (static_cast<int>(lit) != lastShown) {
      lastShown = lit;
      Canvas canvas(frame, WIDTH, HEIGHT);
      canvas.fill(lit ? Rgb{255, 255, 255} : Rgb{0, 0, 0});
      tft.startWrite();
      tft.pushImageDMA(0, 0, WIDTH, HEIGHT, reinterpret_cast<const lgfx::swap565_t*>(frame));
      tft.endWrite();
    }
    wasBlinking = true;
    return;
  }
  if (wasBlinking) {
    wasBlinking = false;
    dirty = true;
  }
  static uint32_t lastFrame = 0;
  const bool fading = haveShown && now - colorAt < FADE_MS + FRAME_MS;
  const bool animating = scene.haveColor && infoUntil == 0 && sceneMoves(sceneFor(effectShown));
  if ((animating || fading || dirty) && now - lastFrame >= FRAME_MS) {
    const float dt = lastFrame == 0 ? FRAME_MS / 1000.0f : (now - lastFrame) / 1000.0f;
    lastFrame = now;
    if (animating) sceneStep(animation, dt);
    draw();
    dirty = false;
  }
}

void screenStatus(Status s, const char* d, int saved) {
  if (s == status && saved == savedNetworks && strcmp(detail, d != nullptr ? d : "") == 0) return;
  status = s;
  savedNetworks = saved;
  snprintf(detail, sizeof(detail), "%s", d != nullptr ? d : "");
  dirty = true;
}

void screenInfo() {
  infoUntil = millis() < infoUntil ? 0 : millis() + INFO_MS;  // a second press closes it
  dirty = true;
  logf("info %s\n", infoUntil ? "shown" : "closed");
}

#else  // no screen in this build

void startScreen() {}
void serviceScreen() {}
void screenStatus(Status, const char*, int) {}
void screenInfo() {}

#endif

}  // namespace cheerlights

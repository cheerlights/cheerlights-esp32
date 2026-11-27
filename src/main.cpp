// CheerLights for ESP32 boards: shows the CheerLights color on LEDs or the T-Display's screen. The color comes from
// a phone over Bluetooth (docs/ble-profile.md) or from CheerLights over Wi-Fi (docs/wifi.md), chosen at setup.
// What the light is wired to, and Wi-Fi, are set over USB (docs/configure.md).
// Every state change prints one line on the serial console.
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>
#include <string.h>

#include <initializer_list>

#include "app.h"
#include "console.h"

#ifndef CL_CHIP
#error "Build with -DCL_CHIP=\"esp32s3\" or another chip name, as platformio.ini does"
#endif

namespace cheerlights {

const char FW_TEXT[] = "0.8.0";
static_assert(FW_MAJOR == 0 && FW_MINOR == 8 && FW_PATCH == 0, "FW_TEXT matches the version");

Settings settings;
Scene scene{};
TextShow textShow;
Flow flow;
bool motion = false;  // a motion sensor answered, so the light has Flow
char deviceName[9] = "CL-0000";
char hostName[17] = "cheerlights-0000";

namespace {

constexpr uint32_t FRAME_INTERVAL_MS = 5;  // 200 frames a second while an effect moves, so dithering does not flicker
constexpr uint32_t SAVE_DELAY_MS = 2000;   // a slider writes many values; flash gets the last one
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr size_t LOG_BUFFER_SIZE = 1024;   // room for a burst of log lines while a reader catches up
constexpr uint32_t PWM_HZ = 5000;
constexpr uint8_t PWM_BITS = 12;  // 4096 steps, so a dim RGB LED fades smoothly and keeps its hue
constexpr uint32_t PWM_MAX = (1u << PWM_BITS) - 1;
constexpr int SCREEN_INFO_PIN = 0;  // the T-Display's left button; its right one is the settings' button

// Chips with a built-in USB port print to it and to UART0, so either cable works.
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
HardwareSerial& uart = Serial0;
Stream* consoles[] = {&Serial, &Serial0};
#else
Stream* consoles[] = {&Serial};
#endif

Preferences prefs;
Settings staged;  // console changes waiting for save
Adafruit_NeoPixel* strip = nullptr;
uint32_t lightChangedAt = 0;
bool lightUnsaved = false;

neoPixelType neoType(ColorOrder order) {
  switch (order) {
    case ColorOrder::RGB: return NEO_RGB + NEO_KHZ800;
    case ColorOrder::RBG: return NEO_RBG + NEO_KHZ800;
    case ColorOrder::GBR: return NEO_GBR + NEO_KHZ800;
    case ColorOrder::BRG: return NEO_BRG + NEO_KHZ800;
    case ColorOrder::BGR: return NEO_BGR + NEO_KHZ800;
    default: return NEO_GRB + NEO_KHZ800;
  }
}

void beginOutput() {
  if (settings.output == Output::Pixels) {
    strip = new Adafruit_NeoPixel(settings.count, settings.pixelPin, neoType(settings.order));
    strip->begin();  // the library's own brightness stays off: render() scales each value once, itself
  } else if (settings.output == Output::Rgb) {
    for (int pin : {settings.redPin, settings.greenPin, settings.bluePin}) ledcAttach(pin, PWM_HZ, PWM_BITS);
  }
}

bool testing = false;  // a test color owns the LEDs (see startTest)
bool blinking = false;
uint32_t blinkStartedAt = 0;
uint8_t residue[MAX_PIXELS * 3];  // what dithering carries to the next frame, per pixel and channel

// Sends the scene to the LEDs. The screen draws itself (screen_out.cpp).
void render() {
  if (testing) return;
  scene.ms = millis();
  const uint8_t level = capBrightness(scene.light.brightness, settings.maxBrightness);
  bool lit = false;
  if (identifying(lit)) {
    // White at the light's own brightness, so a blink is as bright as the color and no brighter.
    const uint8_t white = roundLevel(outputLevel(255, level));
    if (settings.output == Output::Pixels && strip != nullptr) {
      for (int i = 0; i < settings.count; i++) strip->setPixelColor(i, lit ? white : 0, lit ? white : 0, lit ? white : 0);
      strip->show();
    } else if (settings.output == Output::Rgb) {
      const uint32_t duty = lit ? (static_cast<uint32_t>(level) * PWM_MAX + 127u) / 255u : 0;
      for (int pin : {settings.redPin, settings.greenPin, settings.bluePin}) {
        ledcWrite(pin, settings.commonAnode ? PWM_MAX - duty : duty);
      }
    }
    return;
  }
  if (settings.output == Output::Pixels && strip != nullptr) {
    const bool moving = sceneMoves(settings, scene);
    for (int i = 0; i < settings.count; i++) {
      const Rgb8 p = scenePixel(settings, scene, i);
      const uint8_t in[3] = {p.r, p.g, p.b};
      uint8_t out[3];
      for (int k = 0; k < 3; k++) {
        const uint16_t v = outputLevel(in[k], level);
        out[k] = moving ? ditherLevel(v, residue[i * 3 + k]) : roundLevel(v);
      }
      strip->setPixelColor(i, out[0], out[1], out[2]);
    }
    strip->show();
  } else if (settings.output == Output::Rgb) {
    const Rgb8 p = scenePixel(settings, scene, 0);
    const uint8_t in[3] = {p.r, p.g, p.b};
    const int pins[3] = {settings.redPin, settings.greenPin, settings.bluePin};
    for (int i = 0; i < 3; i++) {
      const uint32_t duty = (static_cast<uint32_t>(in[i]) * level * PWM_MAX + 32512u) / 65025u;
      ledcWrite(pins[i], settings.commonAnode ? PWM_MAX - duty : duty);
    }
  }
}

// A fresh device drives no pin: nothing tells it what is wired where, and a wrong guess sends LED data
// into whatever that pin is connected to. It waits for the install page or the console.
void loadSettings() {
  settings = Settings{};
  uint8_t stored[STORED_SIZE];
  Settings saved;
  const bool haveSaved = prefs.getBytes("settings", stored, STORED_SIZE) == STORED_SIZE &&
                         decodeSettings(stored, STORED_SIZE, saved);
  const char* why = haveSaved ? problem(CL_CHIP, saved) : nullptr;
  if (haveSaved && why == nullptr) {
    settings = saved;
  } else if (haveSaved) {
    logf("settings ignored reason=%s\n", why);
  }
  staged = settings;
  char text[200];
  describe(settings, text, sizeof(text));
  logf("settings %s (%s)\n", text, haveSaved && why == nullptr ? "saved" : "not set up");
}

// The test color the install page or a person uses to find the LEDs. It takes over the light until
// `test off`, a save, or a restart, and blanks each pin it leaves, since LEDs hold their last color.
Adafruit_NeoPixel* testStrip = nullptr;
constexpr uint8_t TEST_BRIGHTNESS = 48;  // enough to see, little enough for a long strip on USB power

void stopOutput() {
  if (strip != nullptr) {
    strip->clear();
    strip->show();
    delete strip;
    strip = nullptr;
  } else if (settings.output == Output::Rgb) {
    for (int pin : {settings.redPin, settings.greenPin, settings.bluePin}) {
      ledcWrite(pin, settings.commonAnode ? PWM_MAX : 0);
      ledcDetach(pin);
    }
  }
}

void endTestStrip() {
  if (testStrip == nullptr) return;
  testStrip->clear();
  testStrip->show();
  delete testStrip;
  testStrip = nullptr;
}

void startTest(const TestRequest& t) {
  if (!testing) {
    stopOutput();
    testing = true;
  }
  if (testStrip != nullptr && (testStrip->getPin() != t.pin || testStrip->numPixels() != t.count)) endTestStrip();
  if (testStrip == nullptr) {
    testStrip = new Adafruit_NeoPixel(t.count, t.pin, NEO_RGB + NEO_KHZ800);  // bytes go out as given
    testStrip->begin();
  }
  testStrip->setBrightness(TEST_BRIGHTNESS);
  testStrip->fill(Adafruit_NeoPixel::Color(t.color.r, t.color.g, t.color.b));
  if (t.split) {
    const int half = t.count / 2;
    testStrip->fill(Adafruit_NeoPixel::Color(t.second.r, t.second.g, t.second.b), half, t.count - half);
  }
  testStrip->show();
}

void stopTest() {
  if (!testing) return;
  endTestStrip();
  testing = false;
  beginOutput();
  render();
}

void runCommand(const char* line) {
  static char reply[REPLY_SIZE];
  bool handled = false;
  wifiCommand(line, reply, sizeof(reply), handled);
  if (handled) {
    // Replies go out whole: a network name can be long, and the log line has a fixed size.
    logText(reply);
    logText("\n");
    return;
  }
  if (strcmp(line, "motion") == 0) {
    float x = 0, y = 0, z = 0;
    if (readGravity(x, y, z)) {
      logf("motion x=%.2f y=%.2f z=%.2f\n", x, y, z);
    } else {
      logf("motion none\n");
    }
    return;
  }
  ScrollText text = textShow.text;
  const TextAction textAction = handleTextCommand(line, canScroll(settings), text, reply, sizeof(reply));
  if (textAction != TextAction::NotText) {
    logText(reply);
    logText("\n");
    if (textAction == TextAction::Changed) textChanged(text);
    return;
  }
  TestRequest test{};
  const Action action = handleCommand(line, CL_CHIP, settings, staged, reply, sizeof(reply), &test);
  if (reply[0]) {
    logText(reply);
    logText("\n");
  }
  if (action == Action::About) {
    char net[120];
    wifiAbout(net, sizeof(net));
    logf("about chip=%s fw=%s name=%s setup=%s link=%s%s\n", CL_CHIP, FW_TEXT, deviceName,
         isSetUp(settings) ? "saved" : "none", settings.link == Link::Wifi ? "wifi" : "bluetooth", net);
    return;
  }
  if (action == Action::Test) {
    startTest(test);
    return;
  }
  if (action == Action::TestOff) {
    stopTest();
    return;
  }
  if (action != Action::None) endTestStrip();
  if (action == Action::Save) {
    uint8_t stored[STORED_SIZE];
    encodeSettings(staged, stored);
    prefs.putBytes("settings", stored, STORED_SIZE);
  } else if (action == Action::Reset) {
    prefs.remove("settings");
  }
  if (action != Action::None) {
    delay(200);  // let the reply reach the console
    ESP.restart();
  }
}

// Reads lines from each console; a command can come from either cable.
void pollConsoles() {
  constexpr size_t LINE = WIFI_LINE_SIZE + 40;  // a Wi-Fi name and password in quotes fit
  static char lines[sizeof(consoles) / sizeof(consoles[0])][LINE];
  static size_t lengths[sizeof(consoles) / sizeof(consoles[0])] = {};
  for (size_t i = 0; i < sizeof(consoles) / sizeof(consoles[0]); i++) {
    while (consoles[i]->available() > 0) {
      const int ch = consoles[i]->read();
      if (ch == '\r' || ch == '\n') {
        if (lengths[i] > 0) {
          lines[i][lengths[i]] = '\0';
          lengths[i] = 0;
          runCommand(lines[i]);
        }
      } else if ((ch == 8 || ch == 127) && lengths[i] > 0) {
        lengths[i]--;
      } else if (ch >= 32 && lengths[i] < LINE - 1) {
        lines[i][lengths[i]++] = static_cast<char>(ch);
      }
    }
  }
}

struct Button {
  int pin;
  bool last = true, stable = true;
  uint32_t changedAt = 0;
};

// True once per press.
bool pressed(Button& b) {
  if (b.pin < 0) return false;
  const bool reading = digitalRead(b.pin) == HIGH;
  if (reading != b.last) {
    b.last = reading;
    b.changedAt = millis();
  }
  if (reading != b.stable && millis() - b.changedAt > DEBOUNCE_MS) {
    b.stable = reading;
    return !b.stable;
  }
  return false;
}

Button mainButton{-1}, infoButton{-1};

// LEDs: the button turns the light off and on. The T-Display: the right button steps through the animations,
// and the left one shows the info card.
void pollButtons() {
  if (pressed(mainButton)) {
    Light next = scene.light;
    if (settings.output == Output::Screen) {
      size_t count = 0;
      const EffectInfo* effects = lightEffects(count);
      size_t at = 0;
      for (size_t i = 0; i < count; i++) {
        if (effects[i].id == scene.light.effect) at = i;
      }
      next.effect = effects[(at + 1) % count].id;
    } else {
      next.on = !next.on;
    }
    lightChanged(next);
    // The phone or the API reads the change, the same as one it made itself.
    if (settings.link == Link::Wifi) {
      wifiLightChanged(next);
    } else {
      bleLightChanged(next);
    }
  }
  if (pressed(infoButton)) screenInfo();
}

// Steps the liquid through the time that passed, reading gravity before each step. It runs only while Flow
// shows, and catches up at most a few steps, so a long pause (a restart of Wi-Fi, a slow write) does not stall it.
void serviceFlow() {
  static uint32_t lastStepAt = 0;
  const uint32_t now = millis();
  if (!motion || scene.light.effect != EFFECT_FLOW || !sceneMoves(scene)) {
    lastStepAt = now;
    return;
  }
  constexpr uint32_t STEP_MS = static_cast<uint32_t>(FLOW_STEP_S * 1000.0f + 0.5f);
  int steps = static_cast<int>((now - lastStepAt) / STEP_MS);
  if (steps == 0) return;
  lastStepAt += static_cast<uint32_t>(steps) * STEP_MS;
  if (steps > 4) steps = 4;
  float sx = 0, sy = 0, sz = 0, gx = 0, gy = 0;
  if (!readGravity(sx, sy, sz)) return;
  matrixGravity(sx, sy, gx, gy);
  for (int i = 0; i < steps; i++) flow.step(gx, gy, scene.light.speed);
}

void makeNames() {
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceName, sizeof(deviceName), "CL-%02X%02X", static_cast<uint8_t>(mac >> 32),
           static_cast<uint8_t>(mac >> 40));
  snprintf(hostName, sizeof(hostName), "cheerlights-%02x%02x", static_cast<uint8_t>(mac >> 32),
           static_cast<uint8_t>(mac >> 40));
}

}  // namespace

// Every log line goes to every console. None of them may wait on a reader (see setup).
void logText(const char* text) {
  for (Stream* c : consoles) c->print(text);
}

void logf(const char* format, ...) {
  char line[200];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  logText(line);
}

void showFrame(const Frame& frame, bool haveColor) {
  scene.frame = frame;
  scene.haveColor = haveColor;
  render();
  logf("frame seq=%u idx=%u rgb=%02x%02x%02x age=%u stale=%d off=%d\n", frame.sequence, frame.paletteIndex, frame.r,
       frame.g, frame.b, frame.ageMinutes, frame.stale, !scene.light.on);
}

void lightChanged(const Light& light) {
  scene.light = light;
  lightChangedAt = millis();
  lightUnsaved = true;
  render();
  logf("light brightness=%u effect=%u speed=%u on=%d\n", light.brightness, light.effect, light.speed, light.on);
}

const EffectInfo* lightEffects(size_t& count) { return effectsFor(settings.output, motion, count); }

const ScrollText& currentText() { return textShow.text; }

void textChanged(const ScrollText& text) {
  if (!canScroll(settings) || text == textShow.text) return;
  const bool restart = strcmp(text.message, textShow.text.message) != 0 || text.on != textShow.text.on ||
                       text.speed != textShow.text.speed;
  textShow.text = text;
  layoutText(text.message, textShow.strip);
  // New words, or words turned on, come in from the right edge, so they are read from the start.
  if (restart) textShow.since = millis();
  uint8_t stored[TEXT_STORED_SIZE];
  encodeText(text, stored);
  prefs.putBytes("text", stored, TEXT_STORED_SIZE);
  bleTextChanged(text);
  char line[140];
  describeText(text, line, sizeof(line));
  logf("%s\n", line);
  render();
}

void identify() {
  blinking = true;
  blinkStartedAt = millis();
  logf("identify\n");
  render();
}

bool identifying(bool& lit) {
  if (!blinking) return false;
  const uint32_t elapsed = millis() - blinkStartedAt;
  if (elapsed >= IDENTIFY_MS) {
    blinking = false;
    return false;
  }
  lit = identifyLit(elapsed);
  return true;
}

}  // namespace cheerlights

using namespace cheerlights;

void setup() {
  // Logging must never hold up the light or the radio. Once a PC has opened a native USB port and stopped
  // reading it, each print waits up to 2 s for room, so a full buffer drops lines instead.
  Serial.setTxBufferSize(LOG_BUFFER_SIZE);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxTimeoutMs(0);
  uart.setTxBufferSize(LOG_BUFFER_SIZE);
  uart.begin(115200);
#endif
  Serial.begin(115200);

  makeNames();
  prefs.begin("cheerlights");
  loadSettings();
  motion = startMotion();
  if (motion) {
    const Layout l = layoutOf(settings);
    flow.reset(l.width, l.height);
    scene.flow = &flow;
  }
  uint8_t savedLight[LIGHT_SIZE];
  if (prefs.getBytes("light", savedLight, LIGHT_SIZE) == LIGHT_SIZE) parseLight(savedLight, LIGHT_SIZE, scene.light);
  // On and off is kept on its own, as it was before a phone could set it, so an update keeps the owner's choice.
  scene.light.on = !prefs.getBool("off", false);
  size_t effectCount = 0;
  const EffectInfo* effects = lightEffects(effectCount);
  if (!hasEffect(effects, effectCount, scene.light.effect)) scene.light.effect = EFFECT_SOLID;
  uint8_t savedText[TEXT_STORED_SIZE];
  if (prefs.getBytes("text", savedText, TEXT_STORED_SIZE) == TEXT_STORED_SIZE) {
    decodeText(savedText, TEXT_STORED_SIZE, textShow.text);
    layoutText(textShow.text.message, textShow.strip);
  }
  scene.text = &textShow;
  // Input-only pins (GPIO 34 to 39 on the ESP32) have no pull-up inside the chip; boards wire one on the board.
  if (settings.buttonPin >= 0) {
    pinMode(settings.buttonPin, pinUsable(CL_CHIP, settings.buttonPin, true) ? INPUT_PULLUP : INPUT);
    mainButton.pin = settings.buttonPin;
  }
  if (settings.output == Output::Screen) {
    pinMode(SCREEN_INFO_PIN, INPUT_PULLUP);
    infoButton.pin = SCREEN_INFO_PIN;
    startScreen();
  }
  beginOutput();
  render();
  if (settings.link == Link::Wifi) {
    startWifi();
  } else {
    startBle();
  }
  logf("light brightness=%u effect=%u speed=%u on=%d\n", scene.light.brightness, scene.light.effect, scene.light.speed,
       scene.light.on);
  if (isSetUp(settings)) {
    logf("type help for the settings commands\n");
  } else {
    logf("not set up: open %s, or type boards, then board <name>, then save\n", SETUP_URL);
  }
}

void loop() {
  if (settings.link == Link::Wifi) {
    serviceWifi();
  } else {
    serviceBle();
  }
  if (lightUnsaved && millis() - lightChangedAt > SAVE_DELAY_MS) {
    uint8_t value[LIGHT_MAX_SIZE];
    encodeLight(scene.light, value);
    prefs.putBytes("light", value, LIGHT_SIZE);  // the stored value stays four bytes, so older firmware reads it
    if (prefs.getBool("off", false) == scene.light.on) prefs.putBool("off", !scene.light.on);
    lightUnsaved = false;
  }
  serviceFlow();
  static uint32_t lastFrameAt = 0;
  static bool wasBlinking = false;
  bool lit = false;
  const bool blinkingNow = identifying(lit);
  // One more frame after a blink puts the color back.
  const bool redraw = blinkingNow || wasBlinking || sceneMoves(settings, scene);
  wasBlinking = blinkingNow;
  if (settings.output != Output::Screen && redraw && millis() - lastFrameAt >= FRAME_INTERVAL_MS) {
    lastFrameAt = millis();
    render();
  }
  serviceScreen();
  pollButtons();
  pollConsoles();
  delay(settings.output == Output::Screen ? 1 : 2);
}

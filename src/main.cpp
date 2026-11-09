// CheerLights BLE firmware: shows the color a phone writes over Bluetooth, on any ESP32 with Bluetooth LE.
// Wire format: docs/ble-profile.md. What the light is wired to is set over USB (docs/configure.md).
// Every state change prints one line on the serial console.
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <device.h>
#include <profile.h>
#include <stdarg.h>
#include <string.h>

#include <initializer_list>

using namespace cheerlights;

#ifndef CL_CHIP
#error "Build with -DCL_CHIP=\"esp32s3\" or another chip name, as platformio.ini does"
#endif

namespace {

constexpr const char* SERVICE_UUID = "b73a0a04-bdb7-4051-9135-815aac2abaa3";
constexpr const char* COLOR_UUID = "0fc3891d-11a7-408c-9283-b14dffc5bcff";
constexpr const char* INFO_UUID = "3554cfbc-0bcc-47f2-9af2-3b313c97a59d";
constexpr const char* LIGHT_UUID = "0a8345b7-afa9-47df-9d63-e98130514e19";
constexpr const char* EFFECTS_UUID = "c4fa31c4-e81e-49d2-be0c-530c01865776";

constexpr uint8_t FW_MAJOR = 0, FW_MINOR = 3, FW_PATCH = 0;
constexpr uint8_t CAP_DISPLAY = 0x01, CAP_OFF_CONTROL = 0x02, CAP_LIGHT = 0x04;

constexpr uint32_t FRAME_INTERVAL_MS = 20;  // 50 frames a second while an effect moves
constexpr uint32_t SAVE_DELAY_MS = 2000;    // a slider writes many values; flash gets the last one
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr size_t LOG_BUFFER_SIZE = 1024;    // room for a burst of log lines while a reader catches up
constexpr uint32_t PWM_HZ = 5000;
constexpr const char* SETUP_URL = "https://cheerlights.github.io/cheerlights-esp32-ble/";

// Chips with a built-in USB port print to it and to UART0, so either cable works.
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
HardwareSerial& uart = Serial0;
Stream* consoles[] = {&Serial, &Serial0};
#else
Stream* consoles[] = {&Serial};
#endif

Preferences prefs;
Settings settings;  // what this boot runs with
Settings staged;    // console changes waiting for save
Adafruit_NeoPixel* strip = nullptr;

// Written from the BLE task and read from the main loop.
portMUX_TYPE pendingLock = portMUX_INITIALIZER_UNLOCKED;
Frame pendingFrame{};
volatile bool framePending = false;
Light pendingLight{};
volatile bool lightPending = false;

Scene scene{};
char deviceName[9] = "CL-0000";
uint32_t lightChangedAt = 0;
bool lightUnsaved = false;

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
    strip->begin();
  } else if (settings.output == Output::Rgb) {
    for (int pin : {settings.redPin, settings.greenPin, settings.bluePin}) ledcAttach(pin, PWM_HZ, 8);
  }
}

bool testing = false;  // a test color owns the LEDs (see startTest)

void render() {
  if (testing) return;
  scene.ms = millis();
  const uint8_t level = capBrightness(scene.light.brightness, settings.maxBrightness);
  if (settings.output == Output::Pixels && strip != nullptr) {
    strip->setBrightness(level);
    for (int i = 0; i < settings.count; i++) {
      const Rgb8 p = scenePixel(settings, scene, i);
      strip->setPixelColor(i, p.r, p.g, p.b);
    }
    strip->show();
  } else if (settings.output == Output::Rgb) {
    const Rgb8 p = scenePixel(settings, scene, 0);
    const uint8_t duty[3] = {static_cast<uint8_t>(p.r * level / 255), static_cast<uint8_t>(p.g * level / 255),
                             static_cast<uint8_t>(p.b * level / 255)};
    const int pins[3] = {settings.redPin, settings.greenPin, settings.bluePin};
    for (int i = 0; i < 3; i++) ledcWrite(pins[i], settings.commonAnode ? 255 - duty[i] : duty[i]);
  }
}

// The callbacks run on the Bluetooth task. Each keeps its own copy of the last good value,
// so a rejected write can restore it without touching what the main loop is drawing.
class ColorCallbacks : public NimBLECharacteristicCallbacks {
 public:
  uint8_t accepted[FRAME_SIZE] = {};

 private:
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue value = c->getValue();
    Frame frame{};
    const ParseResult result = parseFrame(value.data(), value.length(), frame);
    if (result == ParseResult::WrongLength) {
      logf("ignored reason=length len=%u\n", static_cast<unsigned>(value.length()));
    } else if (result == ParseResult::UnknownVersion) {
      logf("ignored reason=version byte0=%02x\n", value.data()[0]);
    }
    if (result != ParseResult::Ok) {
      // A read keeps returning the last frame this firmware accepted.
      c->setValue(accepted, FRAME_SIZE);
      return;
    }
    // A newer phone may send a longer frame; a read returns the part this firmware understood.
    memcpy(accepted, value.data(), FRAME_SIZE);
    c->setValue(accepted, FRAME_SIZE);
    portENTER_CRITICAL(&pendingLock);
    pendingFrame = frame;
    framePending = true;
    portEXIT_CRITICAL(&pendingLock);
  }
};

class LightCallbacks : public NimBLECharacteristicCallbacks {
 public:
  uint8_t accepted[LIGHT_SIZE] = {};

 private:
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue value = c->getValue();
    Light parsed{};
    if (parseLight(value.data(), value.length(), parsed) != ParseResult::Ok) {
      logf("ignored reason=light len=%u\n", static_cast<unsigned>(value.length()));
      // A read keeps returning the settings in use.
      c->setValue(accepted, LIGHT_SIZE);
      return;
    }
    encodeLight(parsed, accepted);  // zero brightness or speed reads back as 1
    c->setValue(accepted, LIGHT_SIZE);
    portENTER_CRITICAL(&pendingLock);
    pendingLight = parsed;
    lightPending = true;
    portEXIT_CRITICAL(&pendingLock);
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    logf("connected peer=%s count=%u\n", info.getAddress().toString().c_str(), server->getConnectedCount());
    NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo&, int reason) override {
    logf("disconnected reason=%d count=%u\n", reason, server->getConnectedCount());
    NimBLEDevice::startAdvertising();
  }
};

ColorCallbacks colorCallbacks;
LightCallbacks lightCallbacks;
ServerCallbacks serverCallbacks;

void startBle() {
  // The short name keeps the service UUID in the primary advertisement, and the
  // chip's public address is static, which Android's companion presence requires.
  const uint64_t mac = ESP.getEfuseMac();
  char* name = deviceName;
  snprintf(name, sizeof(deviceName), "CL-%02X%02X", static_cast<uint8_t>(mac >> 32), static_cast<uint8_t>(mac >> 40));
  NimBLEDevice::init(name);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  NimBLEService* service = server->createService(SERVICE_UUID);

  NimBLECharacteristic* color = service->createCharacteristic(
      COLOR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  const uint8_t empty[FRAME_SIZE] = {};
  color->setValue(empty, FRAME_SIZE);
  color->setCallbacks(&colorCallbacks);

  uint8_t caps = CAP_LIGHT;
  if (settings.output != Output::None) caps |= CAP_DISPLAY;
  if (settings.buttonPin >= 0) caps |= CAP_OFF_CONTROL;
  NimBLECharacteristic* info = service->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ);
  const uint8_t infoValue[5] = {PROTOCOL_VERSION, FW_MAJOR, FW_MINOR, FW_PATCH, caps};
  info->setValue(infoValue, sizeof(infoValue));

  NimBLECharacteristic* light = service->createCharacteristic(
      LIGHT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  encodeLight(scene.light, lightCallbacks.accepted);
  light->setValue(lightCallbacks.accepted, LIGHT_SIZE);
  light->setCallbacks(&lightCallbacks);

  uint8_t effectsValue[128];
  const size_t effectsLength = encodeEffects(EFFECTS, EFFECT_COUNT, effectsValue, sizeof(effectsValue));
  NimBLECharacteristic* effects = service->createCharacteristic(EFFECTS_UUID, NIMBLE_PROPERTY::READ);
  effects->setValue(effectsValue, effectsLength);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->enableScanResponse(true);
  advertising->setName(name);
  advertising->start();
  logf("ready name=%s addr=%s fw=%u.%u.%u chip=%s\n", name, NimBLEDevice::getAddress().toString().c_str(), FW_MAJOR,
       FW_MINOR, FW_PATCH, CL_CHIP);
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
  char text[160];
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
      ledcWrite(pin, settings.commonAnode ? 255 : 0);
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
  char reply[1200];
  TestRequest test{};
  const Action action = handleCommand(line, CL_CHIP, settings, staged, reply, sizeof(reply), &test);
  if (reply[0]) {
    logText(reply);
    logText("\n");
  }
  if (action == Action::About) {
    logf("about chip=%s fw=%u.%u.%u name=%s setup=%s\n", CL_CHIP, FW_MAJOR, FW_MINOR, FW_PATCH, deviceName,
         isSetUp(settings) ? "saved" : "none");
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
  static char lines[sizeof(consoles) / sizeof(consoles[0])][96];
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
      } else if (ch >= 32 && lengths[i] < sizeof(lines[i]) - 1) {
        lines[i][lengths[i]++] = static_cast<char>(ch);
      }
    }
  }
}

void pollButton() {
  if (settings.buttonPin < 0) return;
  static bool lastReading = true;
  static bool stable = true;
  static uint32_t changedAt = 0;
  const bool reading = digitalRead(settings.buttonPin) == HIGH;
  if (reading != lastReading) {
    lastReading = reading;
    changedAt = millis();
  }
  if (reading != stable && millis() - changedAt > DEBOUNCE_MS) {
    stable = reading;
    if (!stable) {  // pressed
      scene.off = !scene.off;
      prefs.putBool("off", scene.off);
      logf("light off=%d\n", scene.off);
      render();
    }
  }
}

}  // namespace

void setup() {
  // Logging must never hold up the light or the Bluetooth task. Once a PC has opened a native USB
  // port and stopped reading it, each print waits up to 2 s for room, so a full buffer drops lines instead.
  Serial.setTxBufferSize(LOG_BUFFER_SIZE);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxTimeoutMs(0);
  uart.setTxBufferSize(LOG_BUFFER_SIZE);
  uart.begin(115200);
#endif
  Serial.begin(115200);

  prefs.begin("cheerlights");
  loadSettings();
  scene.off = prefs.getBool("off", false);  // the wearer's off choice survives a restart
  uint8_t savedLight[LIGHT_SIZE];
  if (prefs.getBytes("light", savedLight, LIGHT_SIZE) == LIGHT_SIZE) parseLight(savedLight, LIGHT_SIZE, scene.light);
  if (settings.buttonPin >= 0) pinMode(settings.buttonPin, INPUT_PULLUP);
  beginOutput();
  render();
  startBle();
  logf("light off=%d\n", scene.off);
  logf("light brightness=%u effect=%u speed=%u\n", scene.light.brightness, scene.light.effect, scene.light.speed);
  if (isSetUp(settings)) {
    logf("type help for the settings commands\n");
  } else {
    logf("not set up: open %s, or type boards, then board <name>, then save\n", SETUP_URL);
  }
}

void loop() {
  if (framePending) {
    portENTER_CRITICAL(&pendingLock);
    scene.frame = pendingFrame;
    framePending = false;
    portEXIT_CRITICAL(&pendingLock);
    scene.haveColor = true;
    render();
    logf("frame seq=%u idx=%u rgb=%02x%02x%02x age=%u stale=%d off=%d\n", scene.frame.sequence,
         scene.frame.paletteIndex, scene.frame.r, scene.frame.g, scene.frame.b, scene.frame.ageMinutes,
         scene.frame.stale, scene.off);
  }
  if (lightPending) {
    portENTER_CRITICAL(&pendingLock);
    scene.light = pendingLight;
    lightPending = false;
    portEXIT_CRITICAL(&pendingLock);
    lightChangedAt = millis();
    lightUnsaved = true;
    render();
    logf("light brightness=%u effect=%u speed=%u\n", scene.light.brightness, scene.light.effect, scene.light.speed);
  }
  if (lightUnsaved && millis() - lightChangedAt > SAVE_DELAY_MS) {
    uint8_t value[LIGHT_SIZE];
    encodeLight(scene.light, value);
    prefs.putBytes("light", value, LIGHT_SIZE);
    lightUnsaved = false;
  }
  static uint32_t lastFrameAt = 0;
  if (sceneMoves(scene) && millis() - lastFrameAt >= FRAME_INTERVAL_MS) {
    lastFrameAt = millis();
    render();
  }
  pollButton();
  pollConsoles();
  delay(2);
}

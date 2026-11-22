// The Bluetooth link: a phone writes the color, and the light, over the CheerLights BLE profile (docs/ble-profile.md).
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "app.h"

namespace cheerlights {

namespace {

constexpr const char* SERVICE_UUID = "b73a0a04-bdb7-4051-9135-815aac2abaa3";
constexpr const char* COLOR_UUID = "0fc3891d-11a7-408c-9283-b14dffc5bcff";
constexpr const char* INFO_UUID = "3554cfbc-0bcc-47f2-9af2-3b313c97a59d";
constexpr const char* LIGHT_UUID = "0a8345b7-afa9-47df-9d63-e98130514e19";
constexpr const char* EFFECTS_UUID = "c4fa31c4-e81e-49d2-be0c-530c01865776";
constexpr const char* IDENTIFY_UUID = "ee2d928a-492c-4ef4-a9be-b3dadd663819";
constexpr const char* TEXT_UUID = "ed858a13-e5b3-48eb-993a-2440da02a36c";

// Written from the BLE task and read from the main loop.
portMUX_TYPE pendingLock = portMUX_INITIALIZER_UNLOCKED;
Frame pendingFrame{};
volatile bool framePending = false;
Light pendingLight{};
volatile bool lightPending = false;
volatile bool identifyPending = false;
ScrollText pendingText{};
volatile bool textPending = false;

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

class IdentifyCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue value = c->getValue();
    if (parseIdentify(value.data(), value.length()) != ParseResult::Ok) {
      logf("ignored reason=identify len=%u\n", static_cast<unsigned>(value.length()));
      return;
    }
    identifyPending = true;
  }
};

// The owner's words. A write the light turns down keeps the words it has, and a read says why, so the phone can.
class TextCallbacks : public NimBLECharacteristicCallbacks {
 public:
  ScrollText kept{};  // guarded by pendingLock: the main loop updates it when the console changes the text

 private:
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue value = c->getValue();
    portENTER_CRITICAL(&pendingLock);
    const ScrollText current = kept;
    portEXIT_CRITICAL(&pendingLock);
    ScrollText next{};
    TextRefusal refusal = TextRefusal::None;
    const ParseResult result = parseTextValue(value.data(), value.length(), current, next, refusal);
    uint8_t out[TEXT_VALUE_SIZE];
    if (result != ParseResult::Ok) {
      logf("ignored reason=text len=%u\n", static_cast<unsigned>(value.length()));
      c->setValue(out, encodeTextValue(current, TextRefusal::None, out));
      return;
    }
    if (refusal != TextRefusal::None) logf("text refused reason=%u\n", static_cast<unsigned>(refusal));
    c->setValue(out, encodeTextValue(next, refusal, out));
    portENTER_CRITICAL(&pendingLock);
    kept = next;
    pendingText = next;
    textPending = refusal == TextRefusal::None;
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
IdentifyCallbacks identifyCallbacks;
TextCallbacks textCallbacks;
ServerCallbacks serverCallbacks;
NimBLECharacteristic* lightCharacteristic = nullptr;
NimBLECharacteristic* textCharacteristic = nullptr;

}  // namespace

void startBle() {
  // The short name keeps the service UUID in the primary advertisement, and the
  // chip's public address is static, which Android's companion presence requires.
  NimBLEDevice::init(deviceName);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  NimBLEService* service = server->createService(SERVICE_UUID);

  NimBLECharacteristic* color = service->createCharacteristic(
      COLOR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  const uint8_t empty[FRAME_SIZE] = {};
  color->setValue(empty, FRAME_SIZE);
  color->setCallbacks(&colorCallbacks);

  // A light that is not set up still connects, so the phone can tell its owner to set it up over USB.
  // The T-Display's button steps through the animations, so it is not an off control.
  const bool setUp = isSetUp(settings);
  const bool offControl = settings.buttonPin >= 0 && settings.output != Output::Screen;
  const bool scrolls = canScroll(settings);
  NimBLECharacteristic* info = service->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ);
  uint8_t infoValue[INFO_MAX_SIZE];
  const size_t infoLength =
      encodeInfo({FW_MAJOR, FW_MINOR, FW_PATCH, setUp, offControl, true, !setUp, true, boardName(settings, CL_CHIP), scrolls},
                 infoValue);
  info->setValue(infoValue, infoLength);

  lightCharacteristic = service->createCharacteristic(
      LIGHT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  encodeLight(scene.light, lightCallbacks.accepted);
  lightCharacteristic->setValue(lightCallbacks.accepted, LIGHT_SIZE);
  lightCharacteristic->setCallbacks(&lightCallbacks);

  size_t effectCount = 0;
  const EffectInfo* effects = lightEffects(effectCount);
  uint8_t effectsValue[128];
  const size_t effectsLength = encodeEffects(effects, effectCount, effectsValue, sizeof(effectsValue));
  NimBLECharacteristic* effectsCharacteristic = service->createCharacteristic(EFFECTS_UUID, NIMBLE_PROPERTY::READ);
  effectsCharacteristic->setValue(effectsValue, effectsLength);

  NimBLECharacteristic* identifyCharacteristic =
      service->createCharacteristic(IDENTIFY_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  identifyCharacteristic->setCallbacks(&identifyCallbacks);

  // Only a grid that can show text has Text, and Info bit 5 says so.
  if (scrolls) {
    textCharacteristic = service->createCharacteristic(TEXT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    textCallbacks.kept = currentText();
    uint8_t value[TEXT_VALUE_SIZE];
    textCharacteristic->setValue(value, encodeTextValue(textCallbacks.kept, TextRefusal::None, value));
    textCharacteristic->setCallbacks(&textCallbacks);
  }

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->enableScanResponse(true);
  advertising->setName(deviceName);
  advertising->start();
  screenStatus(Status::Waiting, deviceName);
  logf("ready name=%s addr=%s fw=%s chip=%s link=bluetooth\n", deviceName, NimBLEDevice::getAddress().toString().c_str(),
       FW_TEXT, CL_CHIP);
}

void bleLightChanged(const Light& light) {
  if (lightCharacteristic == nullptr) return;
  encodeLight(light, lightCallbacks.accepted);
  lightCharacteristic->setValue(lightCallbacks.accepted, LIGHT_SIZE);
}

void bleTextChanged(const ScrollText& text) {
  if (textCharacteristic == nullptr) return;
  portENTER_CRITICAL(&pendingLock);
  textCallbacks.kept = text;
  portEXIT_CRITICAL(&pendingLock);
  uint8_t value[TEXT_VALUE_SIZE];
  textCharacteristic->setValue(value, encodeTextValue(text, TextRefusal::None, value));
}

void serviceBle() {
  if (framePending) {
    portENTER_CRITICAL(&pendingLock);
    const Frame frame = pendingFrame;
    framePending = false;
    portEXIT_CRITICAL(&pendingLock);
    showFrame(frame, true);
  }
  if (lightPending) {
    portENTER_CRITICAL(&pendingLock);
    const Light light = pendingLight;
    lightPending = false;
    portEXIT_CRITICAL(&pendingLock);
    lightChanged(light);
  }
  if (textPending) {
    portENTER_CRITICAL(&pendingLock);
    const ScrollText text = pendingText;
    textPending = false;
    portEXIT_CRITICAL(&pendingLock);
    textChanged(text);
  }
  if (identifyPending) {
    identifyPending = false;
    identify();
  }
}

}  // namespace cheerlights

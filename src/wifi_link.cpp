// The Wi-Fi link: the light joins the strongest saved network in range, follows the CheerLights color over MQTT,
// announces itself over mDNS, and takes its light from a small HTTP API (docs/wifi.md).
#include <Arduino.h>
#include <soc/soc_caps.h>
#include <string.h>

#include "app.h"
#include "console.h"
#include "networks.h"
#include "scan.h"
#include "web_page.h"

#if SOC_WIFI_SUPPORTED
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#endif

namespace cheerlights {

#if SOC_WIFI_SUPPORTED

namespace {

constexpr const char* MQTT_HOST = "mqtt.cheerlights.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr const char* MQTT_TOPIC = "cheerlightsRGB";  // retained, so a new subscriber gets the color at once

constexpr uint32_t JOIN_TIMEOUT_MS = 15000;   // then it tries the next saved network in range
constexpr uint32_t RESCAN_MS = 30000;         // with none in range, it looks again this often
constexpr uint32_t MOVED_AFTER_MS = 20000;    // Wi-Fi gone this long: look for another saved network
constexpr uint32_t OFFLINE_AFTER_MS = 15000;  // lost Wi-Fi or CheerLights this long shows the color as stale

Preferences prefs;
Networks networks;
LightState state;      // the owner's mode, fun color, and light
CheerLightsState cheerlights;
uint16_t sequence = 0;  // counts the colors shown, for the log

WiFiClient net;
PubSubClient mqtt(net);
WebServer server(80);
bool serving = false;  // mDNS and the API start on the first connection

// Joining: scan, then try the saved networks in range, strongest first.
enum class Join { Idle, Scanning, Trying, Connected, Waiting };
Join join = Join::Idle;
char trying[33] = "";
int order[MAX_NETWORKS];
int orderCount = 0, orderAt = 0;
bool tryingDirect = false;  // a network just added, tried before any scan
uint32_t joinStartedAt = 0, waitUntil = 0, droppedAt = 0, lostAt = 0;

// The names from the last scan, kept while they are read.
constexpr int SCAN_KEEP = 40;
String scanNames[SCAN_KEEP];

// The store and the saved networks, opened once. A light set to Bluetooth opens them only for the console, so
// Wi-Fi can be saved before the light switches to it.
void loadStore() {
  static bool loaded = false;
  if (loaded) return;
  loaded = true;
  prefs.begin("cheerlights");
  static uint8_t stored[NETWORKS_STORED_SIZE];
  Networks n;
  if (prefs.getBytes("networks", stored, sizeof(stored)) == sizeof(stored) &&
      decodeNetworks(stored, sizeof(stored), n)) {
    networks = n;
  }
}

void saveNetworks() {
  static uint8_t stored[NETWORKS_STORED_SIZE];
  encodeNetworks(networks, stored);
  prefs.putBytes("networks", stored, sizeof(stored));
}

// Shows what the state and the last CheerLights color call for, if that changed.
void refresh() {
  static Frame shown{};
  static bool shownHave = false, first = true;
  bool have = false;
  Frame f = frameFor(state, cheerlights, have);
  if (!first && have == shownHave && f.r == shown.r && f.g == shown.g && f.b == shown.b && f.stale == shown.stale) {
    return;
  }
  first = false;
  f.sequence = ++sequence;
  shown = f;
  shownHave = have;
  showFrame(f, have);
}

void setStatus(Status s) {
  static int last = -1;
  screenStatus(s, trying, networks.count);
  if (static_cast<int>(s) == last) return;
  last = static_cast<int>(s);
  static const char* const NAMES[] = {"no-wifi", "scanning", "joining", "join-failed", "none-in-range", "connecting",
                                      "showing", "waiting"};
  logf("status %s\n", NAMES[static_cast<int>(s)]);
}

void startScan() {
  mqtt.disconnect();
  WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  join = Join::Scanning;
  setStatus(Status::Scanning);
  logf("wifi scanning saved=%d\n", networks.count);
}

void tryNext() {
  if (orderAt >= orderCount) {
    // Nothing left to try: wait, then look again.
    join = Join::Waiting;
    waitUntil = millis() + RESCAN_MS;
    setStatus(orderCount > 0 ? Status::JoinFailed : Status::NoneInRange);
    logf("wifi %s\n", orderCount > 0 ? "none-joined" : "none-in-range");
    return;
  }
  const WifiNetwork& n = networks.list[order[orderAt++]];
  snprintf(trying, sizeof(trying), "%s", n.ssid);
  WiFi.disconnect();
  WiFi.begin(n.ssid, n.password);
  joinStartedAt = millis();
  join = Join::Trying;
  setStatus(Status::Joining);
  logf("wifi joining ssid=\"%s\"\n", trying);
}

// Start over: at boot and after the saved networks change.
void startJoining() {
  mqtt.disconnect();
  if (networks.count == 0) {
    WiFi.disconnect();
    join = Join::Idle;
    trying[0] = '\0';
    setStatus(Status::NoWifi);
    return;
  }
  tryingDirect = false;
  startScan();
}

// A network just added is tried at once; if it does not answer, a scan tries the rest.
void joinNewest() {
  order[0] = 0;
  orderCount = 1;
  orderAt = 0;
  tryingDirect = true;
  tryNext();
}

// ---- The API (docs/wifi.md) ----

const char* outputName() {
  switch (settings.output) {
    case Output::Pixels: return "pixels";
    case Output::Rgb: return "rgb";
    case Output::Screen: return "screen";
    default: return "none";
  }
}

void sendState(int code) {
  static char body[1024];
  size_t count = 0;
  const EffectInfo* effects = lightEffects(count);
  if (writeState(state, cheerlights, effects, count, body, sizeof(body)) == 0) {
    server.send(500, "application/json", "{\"error\":\"The state did not fit.\"}");
    return;
  }
  server.send(code, "application/json", body);
}

void sendError(int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  char body[200];
  serializeJson(doc, body, sizeof(body));
  server.send(code, "application/json", body);
}

void handleInfo() {
  char body[300];
  const DeviceInfo info{deviceName, FW_TEXT, CL_CHIP, boardName(settings, CL_CHIP), outputName()};
  writeInfo(info, body, sizeof(body));
  server.send(200, "application/json", body);
}

void handlePost() {
  // The web server reads a form body into fields, so only a JSON body arrives whole.
  if (!server.hasArg("plain")) {
    sendError(415, "Send the body as JSON, with Content-Type: application/json.");
    return;
  }
  const String& body = server.arg("plain");
  size_t count = 0;
  const EffectInfo* effects = lightEffects(count);
  const LightState before = state;
  const char* why = applyState(body.c_str(), body.length(), effects, count, state);
  if (why != nullptr) {
    logf("api refused reason=%s\n", why);
    sendError(400, why);
    return;
  }
  if (state.fun != before.fun || state.funColor != before.funColor) {
    uint8_t mode[MODE_SIZE];
    encodeMode(state, mode);
    prefs.putBytes("mode", mode, MODE_SIZE);
    logf("mode %s fun=%02x%02x%02x\n", state.fun ? "fun" : "cheerlights", state.funColor.r, state.funColor.g,
         state.funColor.b);
  }
  if (state.light.brightness != before.light.brightness || state.light.effect != before.light.effect ||
      state.light.speed != before.light.speed) {
    lightChanged(state.light);
  }
  refresh();
  sendState(200);
}

void handleOther() {
  const String& uri = server.uri();
  if (uri == "/" || uri == "/api/v1/info" || uri == "/api/v1/state" || uri == "/api/v1/identify") {
    sendError(405, "Not allowed on this path.");
  } else {
    sendError(404, "No such path. The API is at /api/v1/info, /api/v1/state, and /api/v1/identify.");
  }
}

void startServing() {
  if (serving) return;
  serving = true;
  if (MDNS.begin(hostName)) {
    MDNS.setInstanceName(deviceName);
    MDNS.addService("cheerlights", "tcp", 80);
    MDNS.addServiceTxt("cheerlights", "tcp", "api", String(API_VERSION));
    MDNS.addServiceTxt("cheerlights", "tcp", "fw", FW_TEXT);
    MDNS.addServiceTxt("cheerlights", "tcp", "out", outputName());
  } else {
    logf("mdns failed\n");
  }
  server.on("/", HTTP_GET, [] { server.send(200, "text/html; charset=utf-8", WEB_PAGE); });
  server.on("/api/v1/info", HTTP_GET, handleInfo);
  server.on("/api/v1/identify", HTTP_POST, [] {
    identify();
    char body[40];
    snprintf(body, sizeof(body), "{\"identifying\":%lu}", static_cast<unsigned long>(IDENTIFY_MS));
    server.send(200, "application/json", body);
  });
  server.on("/api/v1/state", HTTP_GET, [] { sendState(200); });
  server.on("/api/v1/state", HTTP_POST, handlePost);
  server.onNotFound(handleOther);
  server.begin();
  logf("api ready host=%s.local port=80\n", hostName);
}

void onColor(char*, byte* payload, unsigned int length) {
  Rgb8 next{};
  if (!parseColorPayload(reinterpret_cast<const char*>(payload), length, next)) {
    logf("ignored payload len=%u\n", length);
    return;
  }
  cheerlights.have = true;
  cheerlights.rgb = next;
  cheerlights.live = true;
  lostAt = 0;
  refresh();
}

void serviceJoin() {
  const uint32_t now = millis();
  switch (join) {
    case Join::Scanning: {
      const int found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING) return;
      if (found < 0) {  // the scan failed; look again shortly
        join = Join::Waiting;
        waitUntil = now + 5000;
        return;
      }
      Seen seen[SCAN_KEEP];
      const int count = found < SCAN_KEEP ? found : SCAN_KEEP;
      for (int i = 0; i < count; i++) {
        scanNames[i] = WiFi.SSID(i);
        seen[i] = {scanNames[i].c_str(), WiFi.RSSI(i)};
      }
      orderCount = joinOrder(networks, seen, count, order);
      orderAt = 0;
      WiFi.scanDelete();
      logf("wifi scanned found=%d saved_in_range=%d\n", found, orderCount);
      tryNext();
      break;
    }
    case Join::Trying:
      if (WiFi.status() == WL_CONNECTED) {
        join = Join::Connected;
        const int at = findNetwork(networks, trying);
        if (at > 0) {
          moveToFront(networks, at);  // joined last, so dropped last
          saveNetworks();
        }
        logf("wifi connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        startServing();
      } else if (now - joinStartedAt > JOIN_TIMEOUT_MS) {
        logf("wifi failed ssid=\"%s\"\n", trying);
        if (tryingDirect) {
          tryingDirect = false;
          startScan();
        } else {
          tryNext();
        }
      }
      break;
    case Join::Connected:
      if (WiFi.status() == WL_CONNECTED) {
        droppedAt = 0;
      } else if (droppedAt == 0) {
        droppedAt = now;
        logf("wifi lost\n");
      } else if (now - droppedAt > MOVED_AFTER_MS) {
        droppedAt = 0;
        startScan();  // it may have moved somewhere with another saved network
      }
      break;
    case Join::Waiting:
      if (static_cast<int32_t>(now - waitUntil) >= 0) startScan();
      break;
    default:
      break;
  }
}

}  // namespace

void startWifi() {
  loadStore();
  uint8_t mode[MODE_SIZE];
  if (prefs.getBytes("mode", mode, MODE_SIZE) == MODE_SIZE) decodeMode(mode, MODE_SIZE, state);
  state.light = scene.light;
  size_t count = 0;
  const EffectInfo* effects = lightEffects(count);
  if (!state.fun && (effectFlags(effects, count, state.light.effect) & EFFECT_SHOWS_COLOR) == 0) {
    state.light.effect = EFFECT_SOLID;
    lightChanged(state.light);
  }

  WiFi.persistent(false);  // the networks live in this firmware's own store, with its own rules
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostName);
  WiFi.setAutoReconnect(true);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onColor);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);  // an unreachable broker holds up the light for 5 s at most
  logf("ready name=%s host=%s fw=%s chip=%s link=wifi saved=%d\n", deviceName, hostName, FW_TEXT, CL_CHIP,
       networks.count);
  refresh();  // fun mode shows its color at once, without the network
  startJoining();
}

void serviceWifi() {
  const uint32_t now = millis();
  serviceJoin();
  const bool onWifi = join == Join::Connected && WiFi.status() == WL_CONNECTED;
  if (serving) server.handleClient();
  if (onWifi && !mqtt.connected()) {
    static uint32_t lastTry = 0, wait = 0;
    if (!cheerlights.have) setStatus(Status::Connecting);
    if (now - lastTry >= wait) {
      lastTry = now;
      char id[40];
      snprintf(id, sizeof(id), "%s-%08lx", hostName, static_cast<unsigned long>(esp_random()));
      if (mqtt.connect(id)) {
        mqtt.subscribe(MQTT_TOPIC);
        logf("cheerlights connected\n");
        wait = 0;
      } else {
        wait = wait == 0 ? 2000 : (wait * 2 > 60000 ? 60000 : wait * 2);
        logf("cheerlights unreachable state=%d retry_ms=%lu\n", mqtt.state(), static_cast<unsigned long>(wait));
      }
    }
  }
  mqtt.loop();

  // Stale only after a while, so a short drop does not dim the light.
  const bool lost = !(onWifi && mqtt.connected());
  if (!lost) {
    lostAt = 0;
    if (cheerlights.have && !cheerlights.live) {
      cheerlights.live = true;
      refresh();
    }
  } else if (lostAt == 0) {
    lostAt = now;
  } else if (cheerlights.have && cheerlights.live && now - lostAt > OFFLINE_AFTER_MS) {
    cheerlights.live = false;
    logf("cheerlights lost\n");
    refresh();
  }
}

void wifiLightChanged(const Light& light) { state.light = light; }

void wifiAbout(char* out, size_t size) {
  loadStore();
  if (settings.link != Link::Wifi) {
    snprintf(out, size, " saved=%d", networks.count);
    return;
  }
  const bool on = WiFi.status() == WL_CONNECTED;
  snprintf(out, size, " saved=%d wifi=%s ip=%s cheerlights=%s mode=%s", networks.count, on ? "connected" : "no",
           on ? WiFi.localIP().toString().c_str() : "none", mqtt.connected() ? "connected" : "no",
           state.fun ? "fun" : "cheerlights");
}

// wifi scan, for the console: the networks in range, found now. The loop waits for the scan, about 2 to 4 seconds.
// A Bluetooth light turns Wi-Fi on for the scan only; a Wi-Fi light shares a scan of its own that is running, and
// starts joining again if the scan cut a join short.
void consoleScan(char* out, size_t size) {
  const bool wifiLink = settings.link == Link::Wifi;
  if (!wifiLink) WiFi.mode(WIFI_STA);
  bool shared = false, rejoin = false;
  int found;
  if (wifiLink && join == Join::Scanning) {
    const uint32_t start = millis();
    while ((found = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() - start < 10000) delay(20);
    shared = found >= 0;  // the join reads the same results next
  } else {
    if (wifiLink && join == Join::Trying) {
      WiFi.disconnect();  // a station cannot scan while it is still joining
      rejoin = true;
    }
    found = WiFi.scanNetworks(false, false);
    if (found < 0) {
      delay(200);
      found = WiFi.scanNetworks(false, false);
    }
  }
  if (found < 0) {
    snprintf(out, size, "The scan did not finish. Try wifi scan again.");
  } else {
    ScanSeen seen[SCAN_KEEP];
    const int count = found < SCAN_KEEP ? found : SCAN_KEEP;
    for (int i = 0; i < count; i++) {
      scanNames[i] = WiFi.SSID(i);
      seen[i] = {scanNames[i].c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN};
    }
    formatScan(seen, count, networks, out, size);
    logf("wifi scan found=%d\n", found);
  }
  if (!shared) WiFi.scanDelete();
  if (!wifiLink) WiFi.mode(WIFI_OFF);
  if (rejoin || (wifiLink && join == Join::Scanning && !shared)) startJoining();
}

void wifiCommand(const char* line, char* out, size_t size, bool& handled) {
  loadStore();
  const WifiAction action = handleWifiCommand(line, networks, out, size);
  handled = action != WifiAction::NotWifi;
  if (action == WifiAction::Scan) {
    consoleScan(out, size);
    return;
  }
  if (action == WifiAction::Added || action == WifiAction::Forgotten) {
    saveNetworks();
    const size_t at = strlen(out);
    if (settings.link != Link::Wifi) {
      snprintf(out + at, size - at, " It is used once the light is set to Wi-Fi: link wifi, then save.");
    } else if (action == WifiAction::Added) {
      snprintf(out + at, size - at, " Joining it.");
      mqtt.disconnect();
      joinNewest();
    } else {
      startJoining();
    }
  }
}

#else  // a chip without Wi-Fi

void startWifi() {}
void serviceWifi() {}
void wifiLightChanged(const Light&) {}
void wifiAbout(char* out, size_t size) { snprintf(out, size, "%s", ""); }

void wifiCommand(const char* line, char* out, size_t size, bool& handled) {
  Networks scratch;
  handled = handleWifiCommand(line, scratch, out, size) != WifiAction::NotWifi;
  if (handled) snprintf(out, size, "The %s has no Wi-Fi, so it gets the color from a phone over Bluetooth.", CL_CHIP);
}

#endif

}  // namespace cheerlights

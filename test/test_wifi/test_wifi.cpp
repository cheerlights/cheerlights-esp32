#include <api.h>
#include <console.h>
#include <networks.h>
#include <scan.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

using namespace cheerlights;

void setUp() {}
void tearDown() {}

// ---- Saved networks ----

static WifiAction run(const char* line, Networks& n, char* out, size_t size) {
  return handleWifiCommand(line, n, out, size);
}

static void test_wifi_keeps_names_as_typed_and_never_shows_the_password() {
  Networks w;
  char out[600];
  TEST_ASSERT_EQUAL(WifiAction::Added, run("WIFI \"Home Network 5G\" \"Pass Word 123\"", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(1, w.count);
  TEST_ASSERT_EQUAL_STRING("Home Network 5G", w.list[0].ssid);
  TEST_ASSERT_EQUAL_STRING("Pass Word 123", w.list[0].password);
  TEST_ASSERT_NULL(strstr(out, "Pass Word"));
  run("wifi list", w, out, sizeof(out));
  TEST_ASSERT_NULL(strstr(out, "Pass Word"));
  TEST_ASSERT_EQUAL_STRING("Saved Wi-Fi networks, newest first: \"Home Network 5G\"", out);

  TEST_ASSERT_EQUAL(WifiAction::Added, run("wifi CoffeeShop", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("CoffeeShop", w.list[0].ssid);  // the newest is first, to be joined now
  TEST_ASSERT_EQUAL_STRING("", w.list[0].password);
  const Networks kept = w;
  for (const char* bad : {"wifi Home short", "wifi \"unclosed", "wifi \"\"", "wifi ThisNameIsLongerThanThirtyTwoBytes1",
                          "wifi forget Nowhere", "wifi a b c d e"}) {
    TEST_ASSERT_EQUAL_MESSAGE(WifiAction::None, run(bad, w, out, sizeof(out)), bad);
    TEST_ASSERT_TRUE_MESSAGE(out[0] != '\0', bad);
  }
  TEST_ASSERT_EQUAL(0, memcmp(&kept, &w, sizeof(w)));
  TEST_ASSERT_EQUAL(WifiAction::Forgotten, run("wifi forget CoffeeShop", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(1, w.count);
  TEST_ASSERT_EQUAL(WifiAction::Forgotten, run("wifi forget all", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, w.count);
  run("wifi list", w, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("No saved Wi-Fi networks.", out);

  // Other commands are left to the settings console, untouched.
  for (const char* other : {"help", "wifis", "link wifi", "show", ""}) {
    TEST_ASSERT_EQUAL_MESSAGE(WifiAction::NotWifi, run(other, w, out, sizeof(out)), other);
  }
}

// wifi scan is a command, but a network named scan, list, or all can still be saved and forgotten in quotes.
static void test_command_words_in_quotes_are_network_names() {
  Networks w;
  char out[600];
  TEST_ASSERT_EQUAL(WifiAction::Scan, run("wifi scan", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(WifiAction::Scan, run("  WiFi SCAN ", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, w.count);
  TEST_ASSERT_EQUAL(WifiAction::Added, run("wifi \"scan\"", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(WifiAction::Added, run("wifi \"list\" \"password1\"", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(WifiAction::Added, run("wifi \"all\"", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(3, w.count);
  TEST_ASSERT_EQUAL(WifiAction::None, run("wifi list", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Saved Wi-Fi networks, newest first: \"all\", \"list\", \"scan\"", out);
  TEST_ASSERT_EQUAL(WifiAction::Forgotten, run("wifi forget \"all\"", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(2, w.count);
  TEST_ASSERT_EQUAL_STRING("Forgot \"all\".", out);
  // A quoted "forget" is a network name: here one with a password too short to save.
  TEST_ASSERT_EQUAL(WifiAction::None, run("wifi \"forget\" x", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(2, w.count);
  TEST_ASSERT_EQUAL(WifiAction::Forgotten, run("wifi forget all", w, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, w.count);
}

// The scan reply: one entry per name at its strongest, strongest first, saved and open marked, hidden names and
// names a line cannot carry left out, quotes escaped, and the weakest dropped when the line is full.
static void test_scan_lists_each_network_once_strongest_first() {
  Networks saved;
  addNetwork(saved, "Home", "password1");
  const ScanSeen seen[] = {
      {"Cafe", -71, true},  {"Home", -60, false}, {"", -40, false},          {"Home", -48, false},
      {"Bad\nName", -50, false}, {"Say \"hi\" \\o/", -80, false}, {"Cafe", -90, true}, {nullptr, -30, false},
  };
  char out[600];
  TEST_ASSERT_EQUAL(3, formatScan(seen, 8, saved, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
      "Networks in range, strongest first: \"Home\" -48 dBm saved, \"Cafe\" -71 dBm open, "
      "\"Say \\\"hi\\\" \\\\o/\" -80 dBm",
      out);

  TEST_ASSERT_EQUAL(0, formatScan(seen, 0, saved, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("No Wi-Fi networks in range.", out);
  const ScanSeen hidden[] = {{"", -40, false}};
  TEST_ASSERT_EQUAL(0, formatScan(hidden, 1, saved, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("No Wi-Fi networks in range.", out);

  // A short line keeps the strongest whole entries and never cuts one in half.
  char small[70];
  TEST_ASSERT_EQUAL(1, formatScan(seen, 8, saved, small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("Networks in range, strongest first: \"Home\" -48 dBm saved", small);

  // Forty networks with the longest names still make a line that fits the console's reply.
  static char names[40][33];
  ScanSeen many[40];
  for (int i = 0; i < 40; i++) {
    snprintf(names[i], sizeof(names[i]), "%02d\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"", i);
    many[i] = {names[i], -30 - i, i % 2 == 0};
  }
  static char reply[2400];
  const int listed = formatScan(many, 40, saved, reply, sizeof(reply));
  TEST_ASSERT_TRUE(listed > 10);
  TEST_ASSERT_TRUE(strlen(reply) < sizeof(reply));
  TEST_ASSERT_NOT_NULL(strstr(reply, "\"00\\\""));
}

// A light that moves between places keeps up to eight networks and joins the strongest one in range.
static void test_saved_networks_rotate_and_the_strongest_in_range_goes_first() {
  Networks n;
  char name[33];
  for (int i = 0; i < MAX_NETWORKS; i++) {
    snprintf(name, sizeof(name), "net%d", i);
    addNetwork(n, name, "password1");
  }
  TEST_ASSERT_EQUAL(MAX_NETWORKS, n.count);
  TEST_ASSERT_EQUAL_STRING("net7", n.list[0].ssid);
  TEST_ASSERT_EQUAL_STRING("net0", n.list[MAX_NETWORKS - 1].ssid);
  moveToFront(n, findNetwork(n, "net0"));  // joined net0, so it is kept
  addNetwork(n, "cabin", "password2");      // full: the one joined longest ago goes
  TEST_ASSERT_EQUAL(MAX_NETWORKS, n.count);
  TEST_ASSERT_EQUAL(-1, findNetwork(n, "net1"));
  TEST_ASSERT_NOT_EQUAL(-1, findNetwork(n, "net0"));
  addNetwork(n, "net0", "newpassword");  // the same name updates its password, without a second copy
  TEST_ASSERT_EQUAL(MAX_NETWORKS, n.count);
  TEST_ASSERT_EQUAL_STRING("newpassword", n.list[0].password);

  const Seen seen[] = {{"neighbor", -40}, {"net3", -80}, {"cabin", -55}, {"net3", -60}, {"net5", -55}};
  int order[MAX_NETWORKS];
  const int found = joinOrder(n, seen, 5, order);
  TEST_ASSERT_EQUAL(3, found);
  TEST_ASSERT_EQUAL_STRING("cabin", n.list[order[0]].ssid);  // -55, saved before net5
  TEST_ASSERT_EQUAL_STRING("net5", n.list[order[1]].ssid);
  TEST_ASSERT_EQUAL_STRING("net3", n.list[order[2]].ssid);  // its strongest access point, -60
  TEST_ASSERT_EQUAL(0, joinOrder(n, seen, 1, order));
  TEST_ASSERT_EQUAL(0, joinOrder(Networks{}, seen, 5, order));

  static uint8_t b[NETWORKS_STORED_SIZE];
  encodeNetworks(n, b);
  Networks back;
  TEST_ASSERT_TRUE(decodeNetworks(b, sizeof(b), back));
  TEST_ASSERT_EQUAL(0, memcmp(&n, &back, sizeof(n)));
  b[1] = MAX_NETWORKS + 1;
  TEST_ASSERT_FALSE(decodeNetworks(b, sizeof(b), back));
  encodeNetworks(n, b);
  b[2] = '\0';  // the first network with no name
  TEST_ASSERT_FALSE(decodeNetworks(b, sizeof(b), back));
  TEST_ASSERT_FALSE(decodeNetworks(b, sizeof(b) - 1, back));
}

// ---- The local API (docs/wifi.md) ----

static const char* post(const char* body, LightState& s) {
  return applyState(body, strlen(body), EFFECTS, EFFECT_COUNT, s);
}

static void test_api_info_and_state_have_the_documented_fields() {
  char out[1024];
  const DeviceInfo info{"CL-6B70", "0.5.0", "esp32s3", "Waveshare ESP32-S3-Matrix, 8x8", "pixels"};
  TEST_ASSERT_TRUE(writeInfo(info, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"api\":1,\"name\":\"CL-6B70\",\"fw\":\"0.5.0\",\"chip\":\"esp32s3\","
      "\"board\":\"Waveshare ESP32-S3-Matrix, 8x8\",\"output\":\"pixels\"}",
      out);

  LightState s;
  CheerLightsState cl;
  TEST_ASSERT_TRUE(writeState(s, cl, EFFECTS, EFFECT_COUNT, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"mode\":\"cheerlights\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"cheerlights\":null"));  // before the first color
  cl = {true, {0xff, 0xa5, 0x00}, true};
  writeState(s, cl, EFFECTS, EFFECT_COUNT, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"cheerlights\":{\"name\":\"orange\",\"hex\":\"#ffa500\",\"live\":true}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fun\":{\"hex\":\"#ff00ff\"}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"brightness\":255,\"effect\":0,\"speed\":128"));
  TEST_ASSERT_NOT_NULL(strstr(out, "{\"id\":4,\"name\":\"Rainbow\",\"showsColor\":false}"));
  cl.rgb = {0x12, 0x34, 0x56};
  writeState(s, cl, EFFECTS, EFFECT_COUNT, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":null,\"hex\":\"#123456\""));
  TEST_ASSERT_EQUAL(0, writeState(s, cl, EFFECTS, EFFECT_COUNT, out, 40));  // too small: nothing, not half
}

static void test_api_changes_apply_all_or_nothing_and_keep_cheerlights_mode_honest() {
  LightState s;
  TEST_ASSERT_NULL(post("{\"mode\":\"fun\",\"fun\":{\"hex\":\"#00FF00\"},\"effect\":4,\"brightness\":80}", s));
  TEST_ASSERT_TRUE(s.fun);
  TEST_ASSERT_TRUE(s.funColor == (Rgb8{0, 0xff, 0}));
  TEST_ASSERT_EQUAL_UINT8(EFFECT_RAINBOW, s.light.effect);
  TEST_ASSERT_EQUAL_UINT8(80, s.light.brightness);

  // Each mistake is refused with a reason, and nothing in the request applies.
  const LightState before = s;
  for (const char* bad : {"not json", "[1,2]", "{\"mode\":\"party\"}", "{\"mode\":3}", "{\"fun\":\"#ff0000\"}",
                          "{\"fun\":{\"hex\":\"ff0000\"}}", "{\"fun\":{\"hex\":\"#ff00\"}}",
                          "{\"brightness\":0}", "{\"brightness\":256}", "{\"brightness\":2.5}", "{\"speed\":\"fast\"}",
                          "{\"effect\":9}", "{\"effect\":-1}", "{\"brightness\":10,\"effect\":99}",
                          "{\"mode\":\"cheerlights\",\"effect\":4}"}) {
    LightState t = s;
    TEST_ASSERT_NOT_NULL_MESSAGE(post(bad, t), bad);
    TEST_ASSERT_TRUE_MESSAGE(t.fun == before.fun && t.funColor == before.funColor &&
                                 t.light.brightness == before.light.brightness && t.light.effect == before.light.effect &&
                                 t.light.speed == before.light.speed,
                             bad);
  }

  // Back to CheerLights with a rainbow playing turns the light solid, so it shows the CheerLights color.
  TEST_ASSERT_NULL(post("{\"mode\":\"cheerlights\"}", s));
  TEST_ASSERT_FALSE(s.fun);
  TEST_ASSERT_EQUAL_UINT8(EFFECT_SOLID, s.light.effect);
  TEST_ASSERT_TRUE(s.funColor == (Rgb8{0, 0xff, 0}));  // kept for next time
  TEST_ASSERT_NULL(post("{\"effect\":1,\"speed\":200,\"something\":\"newer\"}", s));  // unknown fields ignored
  TEST_ASSERT_EQUAL_UINT8(EFFECT_BREATHE, s.light.effect);
  TEST_ASSERT_EQUAL_UINT8(200, s.light.speed);

  // A screen has its own effects.
  size_t n = SCREEN_EFFECT_COUNT;
  LightState screen;
  TEST_ASSERT_NULL(applyState("{\"effect\":5}", 12, SCREEN_EFFECTS, n, screen));
  TEST_ASSERT_NOT_NULL(applyState("{\"effect\":1}", 12, SCREEN_EFFECTS, n, screen));
}

static void test_the_light_shows_the_mode_color_and_keeps_its_choice() {
  LightState s;
  CheerLightsState cl;
  bool have = true;
  frameFor(s, cl, have);
  TEST_ASSERT_FALSE(have);  // nothing to show before the first CheerLights color
  cl = {true, {0xff, 0xc0, 0xcb}, true};
  Frame f = frameFor(s, cl, have);
  TEST_ASSERT_TRUE(have);
  TEST_ASSERT_EQUAL_UINT8(10, f.paletteIndex);  // pink, so the LEDs use its picked value
  TEST_ASSERT_FALSE(f.stale);
  cl.live = false;
  TEST_ASSERT_TRUE(frameFor(s, cl, have).stale);  // lost CheerLights: shown dimmed, as a stale phone frame is
  s.fun = true;
  s.funColor = {0x12, 0x34, 0x56};
  f = frameFor(s, cl, have);
  TEST_ASSERT_FALSE(f.stale);
  TEST_ASSERT_EQUAL_UINT8(PALETTE_NONE, f.paletteIndex);
  TEST_ASSERT_EQUAL_UINT8(0x34, f.g);

  Rgb8 c{};
  TEST_ASSERT_TRUE(parseColorPayload(" #FFA500\n", 9, c));
  TEST_ASSERT_TRUE(c == (Rgb8{0xff, 0xa5, 0x00}));
  for (const char* bad : {"orange", "#ffa50", "ffa500", "#ffa5000", "#gga500", ""}) {
    TEST_ASSERT_FALSE_MESSAGE(parseColorPayload(bad, strlen(bad), c), bad);
  }

  uint8_t b[MODE_SIZE];
  encodeMode(s, b);
  LightState back;
  TEST_ASSERT_TRUE(decodeMode(b, MODE_SIZE, back));
  TEST_ASSERT_TRUE(back.fun && back.funColor == s.funColor);
  b[0] = 2;
  TEST_ASSERT_FALSE(decodeMode(b, MODE_SIZE, back));
  TEST_ASSERT_FALSE(decodeMode(b, MODE_SIZE - 1, back));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_keeps_names_as_typed_and_never_shows_the_password);
  RUN_TEST(test_command_words_in_quotes_are_network_names);
  RUN_TEST(test_scan_lists_each_network_once_strongest_first);
  RUN_TEST(test_saved_networks_rotate_and_the_strongest_in_range_goes_first);
  RUN_TEST(test_api_info_and_state_have_the_documented_fields);
  RUN_TEST(test_api_changes_apply_all_or_nothing_and_keep_cheerlights_mode_honest);
  RUN_TEST(test_the_light_shows_the_mode_color_and_keeps_its_choice);
  return UNITY_END();
}

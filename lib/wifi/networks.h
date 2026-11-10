// The Wi-Fi networks a light knows, so it can move between places: at start and whenever it loses Wi-Fi, it scans
// and joins the strongest saved network in range. Passwords stay on the board and are never printed or exported.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cheerlights {

constexpr int MAX_NETWORKS = 8;

struct WifiNetwork {
  char ssid[33];      // up to 32 bytes, as Wi-Fi allows
  char password[64];  // empty for an open network; WPA2 takes 8 to 63 characters
};

struct Networks {
  int count = 0;
  WifiNetwork list[MAX_NETWORKS] = {};  // the most recently added or joined first
};

// Why a name and password cannot be saved, or nullptr when they can.
const char* networkProblem(const char* ssid, const char* password);

// Saves a network at the front, replacing the password of one with the same name. When the list is full, the one
// joined longest ago is dropped. The caller checks networkProblem first.
void addNetwork(Networks& n, const char* ssid, const char* password);
int findNetwork(const Networks& n, const char* ssid);  // its index, or -1
bool forgetNetwork(Networks& n, const char* ssid);
void moveToFront(Networks& n, int index);  // after joining it, so it is dropped last

// A network a scan found.
struct Seen {
  const char* ssid;
  int rssi;  // dBm, higher is stronger
};
// The saved networks in range, as indexes into n.list, strongest first. Returns how many.
int joinOrder(const Networks& n, const Seen* seen, int seenCount, int order[MAX_NETWORKS]);

// Kept in flash as one value.
constexpr size_t NETWORKS_STORED_SIZE = 2 + MAX_NETWORKS * (sizeof(WifiNetwork));
void encodeNetworks(const Networks& n, uint8_t out[NETWORKS_STORED_SIZE]);
bool decodeNetworks(const uint8_t* data, size_t length, Networks& out);

}  // namespace cheerlights

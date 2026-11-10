#include "networks.h"

#include <stdio.h>
#include <string.h>

namespace cheerlights {

const char* networkProblem(const char* ssid, const char* password) {
  const size_t name = strlen(ssid), pass = strlen(password);
  if (name == 0 || name > 32) return "A Wi-Fi name is 1 to 32 characters.";
  if (pass > 63 || (pass > 0 && pass < 8)) return "A Wi-Fi password is 8 to 63 characters, or none for an open network.";
  return nullptr;
}

int findNetwork(const Networks& n, const char* ssid) {
  for (int i = 0; i < n.count; i++) {
    if (strcmp(n.list[i].ssid, ssid) == 0) return i;
  }
  return -1;
}

void moveToFront(Networks& n, int index) {
  if (index <= 0 || index >= n.count) return;
  const WifiNetwork moved = n.list[index];
  memmove(&n.list[1], &n.list[0], index * sizeof(WifiNetwork));
  n.list[0] = moved;
}

void addNetwork(Networks& n, const char* ssid, const char* password) {
  int at = findNetwork(n, ssid);
  if (at < 0) {
    at = n.count < MAX_NETWORKS ? n.count++ : MAX_NETWORKS - 1;  // full: the last is the oldest
  }
  WifiNetwork& w = n.list[at];
  memset(&w, 0, sizeof(w));
  snprintf(w.ssid, sizeof(w.ssid), "%.32s", ssid);
  snprintf(w.password, sizeof(w.password), "%.63s", password);
  moveToFront(n, at);
}

bool forgetNetwork(Networks& n, const char* ssid) {
  const int at = findNetwork(n, ssid);
  if (at < 0) return false;
  memmove(&n.list[at], &n.list[at + 1], (n.count - at - 1) * sizeof(WifiNetwork));
  n.count--;
  memset(&n.list[n.count], 0, sizeof(WifiNetwork));
  return true;
}

int joinOrder(const Networks& n, const Seen* seen, int seenCount, int order[MAX_NETWORKS]) {
  int strength[MAX_NETWORKS];
  int found = 0;
  for (int i = 0; i < n.count; i++) {
    int best = -1000;
    for (int s = 0; s < seenCount; s++) {
      if (seen[s].ssid != nullptr && strcmp(seen[s].ssid, n.list[i].ssid) == 0 && seen[s].rssi > best) {
        best = seen[s].rssi;
      }
    }
    if (best == -1000) continue;
    // Insert by strength; equal strength keeps the saved order.
    int at = found;
    while (at > 0 && strength[at - 1] < best) {
      order[at] = order[at - 1];
      strength[at] = strength[at - 1];
      at--;
    }
    order[at] = i;
    strength[at] = best;
    found++;
  }
  return found;
}

namespace {
constexpr uint8_t STORED_VERSION = 1;
}

void encodeNetworks(const Networks& n, uint8_t out[NETWORKS_STORED_SIZE]) {
  memset(out, 0, NETWORKS_STORED_SIZE);
  out[0] = STORED_VERSION;
  out[1] = static_cast<uint8_t>(n.count);
  memcpy(out + 2, n.list, sizeof(n.list));
}

bool decodeNetworks(const uint8_t* data, size_t length, Networks& out) {
  if (data == nullptr || length != NETWORKS_STORED_SIZE || data[0] != STORED_VERSION || data[1] > MAX_NETWORKS) {
    return false;
  }
  Networks n;
  n.count = data[1];
  memcpy(n.list, data + 2, sizeof(n.list));
  for (int i = 0; i < MAX_NETWORKS; i++) {
    n.list[i].ssid[32] = '\0';
    n.list[i].password[63] = '\0';
    if (i < n.count && networkProblem(n.list[i].ssid, n.list[i].password) != nullptr) return false;
  }
  out = n;
  return true;
}

}  // namespace cheerlights

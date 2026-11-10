#include "scan.h"

#include <stdio.h>
#include <string.h>

namespace cheerlights {

namespace {

constexpr int MAX_LISTED = 64;

// A name the line can carry: 1 to 32 bytes, with no control characters (a newline would end the reply).
bool listable(const char* ssid) {
  const size_t len = strlen(ssid);
  if (len == 0 || len > 32) return false;
  for (const char* p = ssid; *p; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

// The name in quotes, with a backslash before a quote or backslash. Returns the length, or 0 if it does not fit.
size_t quote(const char* ssid, char* out, size_t size) {
  size_t at = 0;
  if (at + 1 >= size) return 0;
  out[at++] = '"';
  for (const char* p = ssid; *p; p++) {
    const bool escape = *p == '"' || *p == '\\';
    if (at + (escape ? 2 : 1) >= size) return 0;
    if (escape) out[at++] = '\\';
    out[at++] = *p;
  }
  if (at + 1 >= size) return 0;
  out[at++] = '"';
  out[at] = '\0';
  return at;
}

}  // namespace

int formatScan(const ScanSeen* seen, int count, const Networks& saved, char* out, size_t size) {
  if (size == 0) return 0;
  // One entry per name, at its strongest.
  int pick[MAX_LISTED];
  int picked = 0;
  for (int i = 0; i < count; i++) {
    if (seen[i].ssid == nullptr || !listable(seen[i].ssid)) continue;
    int same = -1;
    for (int j = 0; j < picked; j++) {
      if (strcmp(seen[pick[j]].ssid, seen[i].ssid) == 0) {
        same = j;
        break;
      }
    }
    if (same >= 0) {
      if (seen[i].rssi > seen[pick[same]].rssi) pick[same] = i;
    } else if (picked < MAX_LISTED) {
      pick[picked++] = i;
    }
  }
  // Strongest first; equal signals keep the scan's order.
  for (int i = 1; i < picked; i++) {
    const int p = pick[i];
    int j = i - 1;
    while (j >= 0 && seen[pick[j]].rssi < seen[p].rssi) {
      pick[j + 1] = pick[j];
      j--;
    }
    pick[j + 1] = p;
  }

  if (picked == 0) {
    snprintf(out, size, "No Wi-Fi networks in range.");
    return 0;
  }
  const char* head = "Networks in range, strongest first:";
  size_t at = static_cast<size_t>(snprintf(out, size, "%s", head));
  if (at >= size) {
    out[0] = '\0';
    return 0;
  }
  int listed = 0;
  for (int i = 0; i < picked; i++) {
    const ScanSeen& s = seen[pick[i]];
    char name[2 * 32 + 3];
    if (quote(s.ssid, name, sizeof(name)) == 0) continue;
    char entry[sizeof(name) + 32];
    const int len = snprintf(entry, sizeof(entry), "%s %s %d dBm%s%s", listed ? "," : "", name, s.rssi,
                             findNetwork(saved, s.ssid) >= 0 ? " saved" : "", s.open ? " open" : "");
    if (len < 0 || at + static_cast<size_t>(len) >= size) break;  // the rest are weaker
    memcpy(out + at, entry, static_cast<size_t>(len) + 1);
    at += static_cast<size_t>(len);
    listed++;
  }
  return listed;
}

}  // namespace cheerlights

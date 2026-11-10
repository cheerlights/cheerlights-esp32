// The reply to wifi scan: the networks in range as one console line, for a person or the install page to read.
#pragma once
#include <stddef.h>

#include "networks.h"

namespace cheerlights {

// A network a scan found, with whether it needs a password.
struct ScanSeen {
  const char* ssid;
  int rssi;  // dBm, higher is stronger
  bool open;
};

// Writes the line, such as
//   Networks in range, strongest first: "Home" -48 dBm saved, "Cafe" -71 dBm open
// or "No Wi-Fi networks in range." A name heard from several access points is listed once, at its strongest. Hidden
// networks, and names with a character a console line cannot carry, are left out. A double quote or backslash in a
// name is written with a backslash before it. Networks that do not fit in out are dropped, weakest first.
// Returns how many are listed.
int formatScan(const ScanSeen* seen, int count, const Networks& saved, char* out, size_t size);

}  // namespace cheerlights

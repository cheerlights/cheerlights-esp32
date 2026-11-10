// The Wi-Fi commands on the USB console: wifi "<name>" "<password>", wifi list, wifi scan, and wifi forget.
// A network is saved at once, not on save, so the light can join it straight away. A password is never printed.
#pragma once
#include <stddef.h>

#include "networks.h"

namespace cheerlights {

// NotWifi: the line is some other command. None: a Wi-Fi command that changed nothing (a list, or a mistake).
// Scan: wifi scan, which the caller runs on the radio; out is left empty.
enum class WifiAction { NotWifi, None, Added, Forgotten, Scan };

// The longest console line: a 32-byte name and a 63-character password, in quotes.
constexpr size_t WIFI_LINE_SIZE = 120;

WifiAction handleWifiCommand(const char* line, Networks& networks, char* out, size_t size);

}  // namespace cheerlights

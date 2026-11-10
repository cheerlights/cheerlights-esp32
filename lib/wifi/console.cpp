#include "console.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace cheerlights {

namespace {

constexpr int MAX_WORDS = 4;

// Splits a line into words, keeping double-quoted text together, and notes which words were quoted, so a network
// named "list" or "scan" can still be saved. Returns how many, or -1 for an unclosed quote, a word too long for its
// slot, or more words than a Wi-Fi command takes.
int split(const char* line, char words[MAX_WORDS][WIFI_LINE_SIZE], bool quoted[MAX_WORDS]) {
  int n = 0;
  const char* p = line;
  while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    if (n == MAX_WORDS) return -1;
    size_t len = 0;
    quoted[n] = *p == '"';
    if (*p == '"') {
      p++;
      while (*p && *p != '"') {
        if (len + 1 >= WIFI_LINE_SIZE) return -1;
        words[n][len++] = *p++;
      }
      if (*p != '"') return -1;
      p++;
    } else {
      while (*p && *p != ' ' && *p != '\t') {
        if (len + 1 >= WIFI_LINE_SIZE) return -1;
        words[n][len++] = *p++;
      }
    }
    words[n][len] = '\0';
    n++;
  }
  return n;
}

bool isWord(const char* word, const char* expected) {
  for (; *word && *expected; word++, expected++) {
    if (tolower(static_cast<unsigned char>(*word)) != *expected) return false;
  }
  return *word == '\0' && *expected == '\0';
}

// A command word: typed bare, never in quotes.
bool isKeyword(const char* word, bool quoted, const char* expected) { return !quoted && isWord(word, expected); }

}  // namespace

WifiAction handleWifiCommand(const char* line, Networks& networks, char* out, size_t size) {
  out[0] = '\0';
  // Only the first word decides whether this is a Wi-Fi command, so other commands never reach the splitter.
  const char* p = line;
  while (*p == ' ' || *p == '\t') p++;
  for (const char* word = "wifi"; *word; p++, word++) {
    if (tolower(static_cast<unsigned char>(*p)) != *word) return WifiAction::NotWifi;
  }
  if (*p != '\0' && *p != ' ' && *p != '\t') return WifiAction::NotWifi;
  static char w[MAX_WORDS][WIFI_LINE_SIZE];
  bool quoted[MAX_WORDS] = {};
  memset(w, 0, sizeof(w));
  const int n = split(line, w, quoted);
  if (n < 0) {
    snprintf(out, size, "Could not read that line. Put a name or password with spaces in double quotes.");
    return WifiAction::None;
  }
  if (n == 2 && isKeyword(w[1], quoted[1], "scan")) return WifiAction::Scan;
  if (n == 2 && isKeyword(w[1], quoted[1], "list")) {
    if (networks.count == 0) {
      snprintf(out, size, "No saved Wi-Fi networks.");
    } else {
      size_t at = static_cast<size_t>(snprintf(out, size, "Saved Wi-Fi networks, newest first:"));
      for (int i = 0; i < networks.count && at < size; i++) {
        at += static_cast<size_t>(snprintf(out + at, size - at, "%s \"%s\"", i ? "," : "", networks.list[i].ssid));
      }
    }
    return WifiAction::None;
  }
  if (n == 3 && isKeyword(w[1], quoted[1], "forget")) {
    if (isKeyword(w[2], quoted[2], "all")) {
      networks = Networks{};
      snprintf(out, size, "All Wi-Fi networks forgotten.");
      return WifiAction::Forgotten;
    }
    if (!forgetNetwork(networks, w[2])) {
      snprintf(out, size, "No saved network \"%.32s\". Type wifi list for the names.", w[2]);
      return WifiAction::None;
    }
    snprintf(out, size, "Forgot \"%.32s\".", w[2]);
    return WifiAction::Forgotten;
  }
  if (n == 2 || n == 3) {
    const char* password = n == 3 ? w[2] : "";
    const char* why = networkProblem(w[1], password);
    if (why != nullptr) {
      snprintf(out, size, "%s", why);
      return WifiAction::None;
    }
    addNetwork(networks, w[1], password);
    snprintf(out, size, "Saved \"%s\".", networks.list[0].ssid);
    return WifiAction::Added;
  }
  snprintf(out, size, "Usage: wifi \"<name>\" \"<password>\", wifi list, wifi scan, or wifi forget \"<name>\"");
  return WifiAction::None;
}

}  // namespace cheerlights

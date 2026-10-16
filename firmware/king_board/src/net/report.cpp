#include "report.h"
#include <WiFi.h>

uint16_t boardFaults(const Status& s) {
  uint16_t f = s.faults;
  if (WiFi.status() != WL_CONNECTED) f = f | Fault::WifiDown;
  return f;
}

String bitNames(uint16_t word, const char* (*name)(int)) {
  String out;
  for (int i = 0; i < 16; i++) {
    if (!(word & (1u << i))) continue;
    if (out.length()) out += ",";
    const char* n = name(i);
    out += n ? String(n) : "bit" + String(i);
  }
  return out.length() ? out : "-";
}

String hex4(uint16_t word) {
  char b[7];
  snprintf(b, sizeof(b), "0x%04X", word);
  return b;
}

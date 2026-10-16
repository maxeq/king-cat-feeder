#include "wifi.h"
#include <WiFi.h>
#include "../../secrets.h"
#include "../clock.h"
#include "../config.h"
#include "../core/log.h"

static bool netUp = false;
static void (*onFirst)() = nullptr;

void wifiOnFirstConnect(void (*fn)()) { onFirst = fn; }

static void connected() {
  netUp = true;
  if (onFirst) {
    onFirst();
    onFirst = nullptr;
  }
}

bool wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_WIFI_PASS);
  Serial.print("WiFi");  // serial-only: progress dots while it connects
  for (int t = 0; WiFi.status() != WL_CONNECTED && t < WIFI_CONNECT_TRIES; t++) {
    delay(WIFI_CONNECT_STEP_MS);
    Serial.print(".");  // serial-only
    logDrain();         // the control task is already running and logging
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println();  // serial-only: end the dots
    warnf("WiFi: no connection at boot, retrying every %lu s", (unsigned long)(WIFI_RETRY_MS / 1000));
    return false;
  }
  IPAddress ip = WiFi.localIP();
  Serial.println();  // serial-only: end the dots
  sayf("WiFi ok, IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  connected();
  return true;
}

void wifiPoll() {
  static Deadline next;
  uint32_t now = millis();
  if (next.running(now)) return;
  next.start(now, WIFI_RETRY_MS);
  if (WiFi.status() != WL_CONNECTED) {
    if (netUp) {
      warnf("WiFi LOST: reconnecting");
      netUp = false;
    }
    WiFi.disconnect();
    WiFi.begin(SECRET_SSID, SECRET_WIFI_PASS);
  } else if (!netUp) {
    IPAddress ip = WiFi.localIP();
    sayf("WiFi ok: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    connected();
  }
}

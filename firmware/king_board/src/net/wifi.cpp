#include "wifi.h"
#include "../core/log.h"
#include "../clock.h"
#include "../../secrets.h"
#include <WiFi.h>

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
  Serial.print("WiFi");
  for (int t = 0; WiFi.status() != WL_CONNECTED && t < 40; t++) {
    delay(500);
    Serial.print(".");
    logDrain();  // the control task is already running and logging
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" NO connection");
    return false;
  }
  Serial.printf(" ok, IP: %s\n", WiFi.localIP().toString().c_str());
  connected();
  return true;
}

void wifiPoll() {
  static unsigned long checkAt = 0;
  if (!due(millis(), checkAt)) return;
  checkAt = millis() + 10000;
  if (WiFi.status() != WL_CONNECTED) {
    if (netUp) {
      say("WiFi LOST — reconnecting");
      netUp = false;
    }
    WiFi.disconnect();
    WiFi.begin(SECRET_SSID, SECRET_WIFI_PASS);
  } else if (!netUp) {
    say("WiFi ok: " + WiFi.localIP().toString());
    connected();
  }
}

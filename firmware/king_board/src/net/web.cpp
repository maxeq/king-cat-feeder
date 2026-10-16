#include <Arduino.h>
#include "web.h"
#include "pages.h"
#include "../../secrets.h"
#include "../version.h"
#include "../app/control.h"
#include "../core/log.h"
#include "../core/watchdog.h"
#include "../drivers/grove.h"
#include "report.h"
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>

static WebServer server(80);
static bool started = false;

static bool authed() {
  if (server.authenticate(SECRET_WEB_USER, SECRET_WEB_PASS)) return true;
  server.requestAuthentication(BASIC_AUTH, "King Feeder");
  return false;
}

void webPoll() {
  if (!started) return;
  ArduinoOTA.handle();
  server.handleClient();
}

// A 20-60 KB frame in one write could hold the loop past the watchdog on a slow link.
// So the length goes in the header and the body goes out in pieces, written straight to
// the socket: one write blocks at most ~10 s, the watchdog is fed after each, and a
// client that stops reading is dropped instead of waited for.
static bool writeAll(const String& data) {
  const size_t PIECE = 2048;
  NetworkClient& client = server.client();
  for (size_t i = 0; i < data.length(); i += PIECE) {
    size_t n = min(PIECE, data.length() - i);
    if (client.write((const uint8_t*)data.c_str() + i, n) != n) {
      client.stop();
      return false;
    }
    watchdogFeed();
  }
  return true;
}

static void sendParts(const char* type, const String& head, const String& body, const String& tail) {
  server.setContentLength(head.length() + body.length() + tail.length());
  server.send(200, type, "");
  writeAll(head) && writeAll(body) && writeAll(tail);
}

// pages come from web/, bundled into pages.h by web/build.py
static void sendGz(const uint8_t* data, size_t len) {
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8", (const char*)data, len);
}

static void handleStatus() {
  Status s = controlStatus();
  String out = String("WEIGHT: ") + (s.scaleOk ? String(s.grams, 1) + " g" : "n/a") + "\n";
  out += String("PIR: ") + (s.pir ? "MOTION" : "quiet") + "\n";
  out += String("DOOR: ") + (s.doorClosed ? "closed" : "OPEN") + " (" + String(s.doorAngle) + "°)\n";
  out += "LIGHT: " + String(s.lightLevel * 100 / 255) + "%\n";
  out += String("AUTO: ") + (s.autoMode ? "ON" : "off");
  if (s.dosing) out += "\nDOSING: " + String(s.dispensed, 1) + " g so far";
  if (s.alarm != feeder::Alarm::None) out += String("\n!!! ALARM: ") + feeder::alarmText(s.alarm);
  uint16_t faults = boardFaults(s);
  out += "\nFAULTS: " + hex4(faults) + " " + bitNames(faults, faultName);
  out += "\nSTATE: " + hex4(s.states) + " " + bitNames(s.states, stateName);
  out += "\nFW: " FW_VERSION " (" FW_GIT ")";
  server.send(200, "text/plain", out);
}

// camera views: answer at once with the latest frame and ask the i2c worker for a
// fresh one, so the web never waits on the camera and dosing never waits on the web
static void handleFrame() {
  aiRequestFrame();
  server.sendHeader("Cache-Control", "no-store");
  unsigned long since = strtoul(server.arg("since").c_str(), nullptr, 10);  // millis() can pass 2^31
  AiStatus st = aiStatus();
  if (st.at && st.at == since) {  // the page already has this frame: skip copying the image
    server.send(200, "application/json",
                "{\"same\":true,\"ok\":" + String(st.online ? "true" : "false") + "}");
    return;
  }
  AiResult ai = aiLatest();
  String head =
      "{\"at\":" + String(ai.at) + ",\"pending\":" + String(aiBusy() ? "true" : "false") + ",\"img\":\"";
  String tail = "\",\"count\":" + String(ai.count) + ",\"best\":" + String(ai.best) +
                ",\"ok\":" + String(ai.online ? "true" : "false") + ",\"ms\":" + String(ai.ms) + "}";
  sendParts("application/json", head, ai.image, tail);
}

static void handleCam() {
  aiRequestFrame();
  AiResult ai = aiLatest();
  String page = FPSTR(CAM_HTML);
  String age = ai.at ? String((millis() - ai.at) / 1000) + " s ago" : "never";
  page.replace("%VERDICT%", ai.verdict + " · updated " + age);
  int at = page.indexOf("%FRAME%");
  String head = page.substring(0, at), tail = page.substring(at + 7);
  if (ai.image.length()) {
    head += "<img src='data:image/jpeg;base64,";
    tail = "'>" + tail;
    sendParts("text/html", head, ai.image, tail);
  } else {
    sendParts("text/html", head + "<p>no frame yet (AI: " + String(ai.online ? "online" : "silent") + ")</p>",
              "", tail);
  }
}

// ?v=0..100
static void handleBright() {
  int pct = controlStatus().lightMaxLevel * 100 / 255;
  if (server.hasArg("v")) {
    pct = constrain(server.arg("v").toInt(), 0, 100);
    controlSend(Cmd::LightPct, pct);
  }
  server.send(200, "application/json", "{\"pct\":" + String(pct) + "}");
}

// ?m=on | pir
static void handleLight() {
  controlSend(server.arg("m") == "on" ? Cmd::LightAlways : Cmd::LightMotion);
  server.send(200, "text/plain", "ok");
}

static void setupOTA() {
  ArduinoOTA.setHostname("kingboard");
  if (strlen(SECRET_OTA_PASS)) ArduinoOTA.setPassword(SECRET_OTA_PASS);
  ArduinoOTA.onStart([]() {
    watchdogUnsubscribe();  // the whole upload runs inside one handle() call
    controlSend(Cmd::SafeStop);
    delay(3 * CONTROL_TICK_MS);  // let the control task reach the safe state
    Serial.println("OTA: receiving firmware...");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA: done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("OTA: error %u\n", e);
    controlSend(Cmd::Resume);
    watchdogSubscribe();
  });
  ArduinoOTA.begin();
}

static void route(const char* path, void (*handler)()) {
  server.on(path, [handler]() {
    if (authed()) handler();
  });
}

void webBegin() {
  setupOTA();
  route("/", []() { sendGz(INDEX_GZ, INDEX_GZ_LEN); });
  route("/status", handleStatus);
  route("/log", []() { server.send(200, "text/plain; charset=utf-8", logText()); });
  route("/cats", []() { sendGz(CATS_GZ, CATS_GZ_LEN); });  // mockup with sample data
  route("/frame", handleFrame);
  route("/cam", handleCam);
  route("/bright", handleBright);
  route("/light", handleLight);
  route("/health", []() {  // the two status words, for telemetry and dashboards
    Status s = controlStatus();
    uint16_t faults = boardFaults(s);
    server.send(200, "application/json",
                "{\"faults\":" + String(faults) + ",\"faultNames\":\"" + bitNames(faults, faultName) +
                    "\",\"critical\":" + String((faults & CRITICAL_FAULTS) ? "true" : "false") +
                    ",\"states\":" + String(s.states) + ",\"stateNames\":\"" + bitNames(s.states, stateName) +
                    "\",\"tickMaxUs\":" + String(s.tickMaxUs) + ",\"overruns\":" + String(s.overruns) +
                    ",\"uptimeS\":" + String(millis() / 1000) + "}");
  });
  route("/version", []() {
    server.send(200, "application/json",
                "{\"version\":\"" FW_VERSION "\",\"git\":\"" FW_GIT "\",\"built\":\"" FW_BUILT "\"}");
  });
  server.begin();
  started = true;
  Serial.printf(">>> web: http://%s/\n", WiFi.localIP().toString().c_str());
}

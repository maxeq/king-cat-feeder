#include "web.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include "../../ota_pubkey.h"
#include "../../secrets.h"
#include "../app/control.h"
#include "../core/lock.h"
#include "../core/log.h"
#include "../core/store.h"
#include "../core/watchdog.h"
#include "../app/camera.h"
#include "../version.h"
#include "pages.h"
#include "report.h"

static_assert(sizeof(SECRET_OTA_PASS) > 1,
              "set SECRET_OTA_PASS in secrets.h: without it anyone on the LAN can start an OTA upload");
#ifndef UPDATE_SIGN
#error "OTA images must be signed: build with the profile, so build_opt.h adds -DUPDATE_SIGN"
#endif

static WebServer server(80);
static bool started = false;
static char logBuf[24 * 220];  // /log; the loop task only

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

// Headers through the server (a few hundred bytes into an empty socket buffer), then the
// body in pieces straight to the socket against one deadline for the whole response. A
// client that stops reading is dropped instead of waited for, so the loop never stalls.
struct Part {
  const char* data;
  size_t len;
};

static void reply(int code, const char* type, const Part* parts, int n) {
  size_t total = 0;
  for (int i = 0; i < n; i++)
    total += parts[i].len;
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(total);
  server.send(code, type, "");
  NetworkClient& client = server.client();
  uint32_t deadline = millis() + WEB_SEND_MS;
  for (int i = 0; i < n; i++)
    if (!sockSend(client.fd(), parts[i].data, parts[i].len, deadline)) {
      client.stop();
      return;
    }
}

static void reply(int code, const char* type, const char* body) {
  Part p{body, strlen(body)};
  reply(code, type, &p, 1);
}

// pages come from web/, bundled into pages.h by web/build.py
static void sendGz(const uint8_t* data, size_t len) {
  server.sendHeader("Content-Encoding", "gzip");
  Part p{(const char*)data, len};
  reply(200, "text/html; charset=utf-8", &p, 1);
}

// state changes need POST plus a custom header: a page on another site can't add one
// without a CORS preflight, which this server never answers, so it can't drive the feeder
static bool fromOurPage() {
  if (server.method() == HTTP_POST && server.header("X-King") == "1") return true;
  reply(403, "text/plain", "POST with X-King: 1");
  return false;
}

static void handleStatus() {
  Status s = controlStatus();
  if (!s.valid) return reply(503, "text/plain", "status unavailable (lock timeout)");
  uint16_t faults = boardFaults(s);
  char fnames[160], snames[160];
  bitNames(faults, faultName, fnames, sizeof(fnames));
  bitNames(s.states, stateName, snames, sizeof(snames));
  char weight[16], dosing[48] = "", alarm[96] = "";
  if (s.scaleOk)
    snprintf(weight, sizeof(weight), "%.1f g", s.grams);
  else
    strlcpy(weight, "n/a", sizeof(weight));
  if (s.dosing) snprintf(dosing, sizeof(dosing), "\nDOSING: %.1f g so far", s.dispensed);
  if (s.alarm != feeder::Alarm::None)
    snprintf(alarm, sizeof(alarm), "\n!!! ALARM: %s", feeder::alarmText(s.alarm));
  char out[768];
  snprintf(
      out, sizeof(out),
      "WEIGHT: %s\nPIR: %s\nDOOR: %s (%d deg)\nLIGHT: %d%%\nAUTO: %s%s%s\nFAULTS: 0x%04X %s\nSTATE: 0x%04X "
      "%s\nRESET: %s\nFW: " FW_VERSION " (" FW_GIT ")",
      weight, s.pir ? "MOTION" : "quiet", s.doorClosed ? "closed" : "OPEN", s.doorAngle,
      s.lightLevel * 100 / 255, s.autoMode ? "ON" : "off", dosing, alarm, faults, fnames, s.states, snames,
      resetReason());
  reply(200, "text/plain; charset=utf-8", out);
}

static void handleHealth() {  // the status words and every margin, for telemetry and dashboards
  Status s = controlStatus();
  uint16_t faults = boardFaults(s);
  char fnames[160], snames[160];
  bitNames(faults, faultName, fnames, sizeof(fnames));
  bitNames(s.states, stateName, snames, sizeof(snames));
  char out[900];
  snprintf(
      out, sizeof(out),
      "{\"valid\":%s,\"faults\":%u,\"faultNames\":\"%s\",\"critical\":%s,\"states\":%u,\"stateNames\":\"%s\","
      "\"tickMaxUs\":%lu,\"overruns\":%lu,\"uptimeS\":%lu,\"resetReason\":\"%s\","
      "\"heapFree\":%u,\"heapMaxBlock\":%u,\"psramFree\":%u,"
      "\"stackFree\":{\"control\":%lu,\"loop\":%lu,\"i2c\":%lu,\"nvs\":%lu},"
      "\"logDropped\":%lu,\"lockTimeouts\":%lu,\"warnings\":%lu,\"errors\":%lu}",
      s.valid ? "true" : "false", faults, fnames, (faults & CRITICAL_FAULTS) ? "true" : "false", s.states,
      snames, (unsigned long)s.tickMaxUs, (unsigned long)s.overruns, (unsigned long)(millis() / 1000),
      resetReason(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
      (unsigned)ESP.getFreePsram(), (unsigned long)s.stackFree,
      (unsigned long)uxTaskGetStackHighWaterMark(nullptr), (unsigned long)cameraStackFree(),
      (unsigned long)storeStackFree(), (unsigned long)logDropped(), (unsigned long)lockTimeouts.load(),
      (unsigned long)logWarnings(), (unsigned long)logErrors());
  reply(200, "application/json", out);
}

// camera views: answer at once with the latest frame and ask the i2c worker for a
// fresh one, so the web never waits on the camera and dosing never waits on the web
static void handleFrame() {
  cameraRequestFrame();
  uint32_t since = strtoul(server.arg("since").c_str(), nullptr, 10);  // millis() can pass 2^31
  AiStatus st = cameraStatus();
  char head[96], tail[160];
  if (st.at && st.at == since) {  // the page already has this frame: skip the image
    snprintf(head, sizeof(head), "{\"same\":true,\"ok\":%s}", st.online ? "true" : "false");
    return reply(200, "application/json", head);
  }
  AiFrame f = cameraPin();  // held while it streams out; the worker writes into another buffer
  snprintf(head, sizeof(head), "{\"at\":%lu,\"pending\":%s,\"img\":\"", (unsigned long)f.result.at,
           cameraBusy() ? "true" : "false");
  snprintf(tail, sizeof(tail), "\",\"count\":%d,\"best\":%d,\"ok\":%s,\"ms\":%lu}", f.result.count,
           f.result.best, f.result.online ? "true" : "false", (unsigned long)f.result.ms);
  Part parts[] = {{head, strlen(head)}, {f.image ? f.image : "", f.len}, {tail, strlen(tail)}};
  reply(200, "application/json", parts, 3);
  cameraRelease();
}

static void handleCam() {  // cam.html with %FRAME% (an <img> or a note), then %VERDICT%
  cameraRequestFrame();
  const char* page = CAM_HTML;
  const char* frame = strstr(page, "%FRAME%");
  const char* verdict = frame ? strstr(frame, "%VERDICT%") : nullptr;
  if (!verdict) return reply(500, "text/plain", "cam.html: markers missing");
  AiFrame f = cameraPin();
  char note[96], text[224];
  uint32_t age = f.result.at ? (millis() - f.result.at) / 1000 : 0;
  if (f.result.at)
    snprintf(text, sizeof(text), "%s · updated %lu s ago", f.result.verdict, (unsigned long)age);
  else
    snprintf(text, sizeof(text), "%s · updated never", f.result.verdict);
  snprintf(note, sizeof(note), "<p>no frame yet (AI: %s)</p>", f.result.online ? "online" : "silent");
  const char* imgOpen = "<img src='data:image/jpeg;base64,";
  bool img = f.image && f.len;
  Part parts[] = {{page, (size_t)(frame - page)},
                  {img ? imgOpen : note, strlen(img ? imgOpen : note)},
                  {img ? f.image : "", img ? f.len : 0},
                  {img ? "'>" : "", img ? 2u : 0u},
                  {frame + 7, (size_t)(verdict - frame - 7)},
                  {text, strlen(text)},
                  {verdict + 9, strlen(verdict + 9)}};
  reply(200, "text/html; charset=utf-8", parts, 7);
  cameraRelease();
}

// GET: the "on" level in %; POST v=0..100 sets it
static void handleBright() {
  int pct = controlStatus().lightMaxLevel * 100 / 255;
  if (server.method() == HTTP_POST) {
    if (!fromOurPage()) return;
    pct = constrain(server.arg("v").toInt(), 0, 100);
    controlSend(Cmd::LightPct, pct);
  }
  char out[24];
  snprintf(out, sizeof(out), "{\"pct\":%d}", pct);
  reply(200, "application/json", out);
}

// POST m=on | pir
static void handleLight() {
  if (!fromOurPage()) return;
  controlSend(server.arg("m") == "on" ? Cmd::LightAlways : Cmd::LightMotion);
  reply(200, "text/plain", "ok");
}

static void setupOTA() {
  // only images signed with the private key behind ota_pubkey.h are accepted (scripts/ota.sh)
  static UpdaterECDSAVerifier key(OTA_PUBKEY, OTA_PUBKEY_LEN, HASH_SHA256);
  ArduinoOTA.setHostname("kingboard");
  ArduinoOTA.setPassword(SECRET_OTA_PASS);
  ArduinoOTA.setSignature(&key);
  ArduinoOTA.onStart([]() {
    watchdogUnsubscribe();  // the whole upload runs inside one handle() call
    if (!controlHalt()) errorf("OTA: the control task did not confirm its safe state");
    say("OTA: receiving firmware...");
  });
  ArduinoOTA.onEnd([]() { say("OTA: done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) {
    errorf("OTA: error %u (a bad signature ends here too)", e);
    controlResume();
    watchdogSubscribe();
  });
  ArduinoOTA.begin();
}

static void route(const char* path, HTTPMethod method, void (*handler)()) {
  server.on(path, method, [handler]() {
    if (authed()) handler();
  });
}

void webBegin() {
  setupOTA();
  static const char* headers[] = {"X-King"};
  server.collectHeaders(headers, 1);
  route("/", HTTP_GET, []() { sendGz(INDEX_GZ, INDEX_GZ_LEN); });
  route("/status", HTTP_GET, handleStatus);
  route("/log", HTTP_GET, []() {
    size_t n = logText(logBuf, sizeof(logBuf));
    Part p{logBuf, n};
    reply(200, "text/plain; charset=utf-8", &p, 1);
  });
  route("/cats", HTTP_GET, []() { sendGz(CATS_GZ, CATS_GZ_LEN); });  // mockup with sample data
  route("/frame", HTTP_GET, handleFrame);
  route("/cam", HTTP_GET, handleCam);
  route("/bright", HTTP_ANY, handleBright);
  route("/light", HTTP_ANY, handleLight);
  route("/health", HTTP_GET, handleHealth);
  route("/version", HTTP_GET, []() {
    reply(200, "application/json",
          "{\"version\":\"" FW_VERSION "\",\"git\":\"" FW_GIT "\",\"built\":\"" FW_BUILT "\"}");
  });
  server.begin();
  started = true;
  IPAddress ip = WiFi.localIP();
  sayf(">>> web: http://%u.%u.%u.%u/", ip[0], ip[1], ip[2], ip[3]);
  if (!strcmp(SECRET_WEB_PASS, "change-me") || !strcmp(SECRET_OTA_PASS, "change-me-too"))
    warnf("security: a password in secrets.h is still the example one: change it");
}

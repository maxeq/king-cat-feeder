// One-letter commands over serial or telnet (port 23). Runs in the loop task;
// every command is a message to the control task, so nothing here touches hardware.
// Telnet asks for the web password first; serial needs none (it takes a cable).
#include "console.h"
#include <WiFi.h>
#include "../../secrets.h"
#include "../app/control.h"
#include "../clock.h"
#include "../config.h"
#include "../core/log.h"
#include "../app/camera.h"
#include "../version.h"
#include "report.h"

static const int NUDGE_DEG = 5;    // door calibration step, '<' and '>'
static const int LOGIN_TRIES = 3;  // wrong passwords before the connection is dropped

static WiFiServer tserver(23);
static WiFiClient tclient;
static bool started = false;
static bool loggedIn = false;
static Deadline loginBy;
static char typed[48];
static size_t typedLen = 0;
static int tries = 0;

#define BANNER "=== king board " FW_VERSION ": a/A auto | f/b/s,1..9 | o/c/q | L/l | p | g | ? ==="

// a peer that stops reading (a laptop gone to sleep) is dropped rather than waited for
static bool telnetLine(const char* m) {
  uint32_t deadline = millis() + TELNET_LINE_MS;
  int fd = tclient.fd();
  if (sockSend(fd, m, strlen(m), deadline) && sockSend(fd, "\r\n", 2, deadline)) return true;
  tclient.stop();
  loggedIn = false;
  warnf("telnet: client too slow, disconnected");  // after loggedIn = false: no mirror loop
  return false;
}

static void toTelnet(const char* m) {
  if (loggedIn && tclient && tclient.connected()) telnetLine(m);
}

void consoleBegin() {
  tserver.begin();
  logMirror(toTelnet);
  started = true;
}

void heartbeat() {
  static Deadline next;
  uint32_t now = millis();
  if (next.running(now)) return;
  next.start(now, HEARTBEAT_MS);
  Status s = controlStatus();
  char line[96];
  if (s.scaleOk)
    snprintf(line, sizeof(line), "· pir=%d light=%d weight=%.1fg", s.pir, s.lightLevel, s.grams);
  else
    snprintf(line, sizeof(line), "· pir=%d light=%d", s.pir, s.lightLevel);
  logQuiet(line);
}

static void send(Cmd c, int arg = 0) {
  if (!controlSend(c, arg)) say("busy: command queue full");
}

static void printStatus(const Status& s) {
  uint16_t faults = boardFaults(s);
  char names[160], weight[16];
  bitNames(faults, faultName, names, sizeof(names));
  if (s.scaleOk)
    snprintf(weight, sizeof(weight), "%.1fg", s.grams);
  else
    strlcpy(weight, "n/a", sizeof(weight));
  IPAddress ip = WiFi.localIP();
  sayf(
      "IP %u.%u.%u.%u | light %d | PIR %d | weight %s | auto %s | heap %uk free, %uk max block | faults "
      "0x%04X %s "
      "| tick max %luus, overruns %lu | fw " FW_VERSION " (" FW_GIT ")",
      ip[0], ip[1], ip[2], ip[3], s.lightLevel, s.pir, weight, s.autoMode ? "ON" : "off",
      (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024), faults, names,
      (unsigned long)s.tickMaxUs, (unsigned long)s.overruns);
}

static void command(char ch) {
  Status s = controlStatus();
  switch (ch) {
    case 'A':
      send(Cmd::AutoOn);
      break;
    case 'a':
      send(Cmd::AutoOff);
      break;
    case 'f':
      send(Cmd::RotorFwd);
      break;
    case 'b':
      send(Cmd::RotorBack);
      break;
    case 's':
      send(Cmd::RotorStop);
      break;
    case 'o':
      send(Cmd::DoorOpen);
      break;
    case 'c':
      send(Cmd::DoorClose);
      break;
    case 'q':
      send(Cmd::DoorRelease);
      break;
    case '<':
      send(Cmd::DoorNudge, +NUDGE_DEG);
      break;  // nudge 5 deg to calibrate (mirrored scale)
    case '>':
      send(Cmd::DoorNudge, -NUDGE_DEG);
      break;
    case 'v':
      cameraRequestInvoke();
      break;  // Grove commands are queued; results arrive in the log
    case 'S':
      cameraRequestSnapshot();
      break;
    case 'g':
      cameraRequestScan();
      break;
    case 'L':
      send(Cmd::LightOn);
      break;
    case 'l':
      send(Cmd::LightOff);
      break;
    case 'p':
      say(s.pir ? "PIR now: MOTION" : "PIR now: quiet");
      break;
    case 'T':
      send(Cmd::PirSelfTest);
      break;
    case 'I':
      send(Cmd::IndicatorOn);
      break;
    case 'i':
      send(Cmd::IndicatorOff);
      break;
    case 'w':
      if (s.scaleOk)
        sayf("weight: %.1f g", s.grams);
      else
        say("scale not responding");
      break;
    case 'r':
      send(Cmd::RawDump);
      break;
    case 't':
      send(Cmd::Tare);
      break;
    case '?':
      printStatus(s);
      break;
    default:
      if (ch >= '1' && ch <= '9')
        send(Cmd::RotorPulse, (ch - '0') * 100);  // the key is the length: '3' = 300 ms
      break;
  }
}

static bool samePassword(const char* a, size_t alen, const char* b) {  // same time for any wrong guess
  size_t blen = strlen(b);
  unsigned diff = alen != blen;
  for (size_t i = 0; i < blen; i++)
    diff |= (unsigned)(i < alen ? a[i] : 0) ^ (unsigned)b[i];
  return diff == 0;
}

static void login(char c) {
  if (c == '\r') return;
  if (c != '\n') {
    if (typedLen < sizeof(typed)) typed[typedLen++] = c;
    return;
  }
  if (samePassword(typed, typedLen, SECRET_WEB_PASS)) {
    loggedIn = true;
    loginBy.stop();
    telnetLine(BANNER);
  } else if (++tries >= LOGIN_TRIES) {
    telnetLine("wrong password, bye");
    tclient.stop();
  } else {
    telnetLine("wrong password");
  }
  typedLen = 0;
}

void consolePoll() {
  if (started && tserver.hasClient()) {
    if (tclient) tclient.stop();
    tclient = tserver.accept();
    loggedIn = false;
    typedLen = 0;
    tries = 0;
    loginBy.start(millis(), TELNET_LOGIN_MS);
    telnetLine("king board " FW_VERSION ", password:");
  }
  if (tclient && !loggedIn && loginBy.expired(millis())) {
    telnetLine("login timed out");
    tclient.stop();
  }
  if (Serial.available()) {
    command((char)Serial.read());
  } else if (tclient && tclient.connected() && tclient.available()) {
    char c = (char)tclient.read();
    if (loggedIn)
      command(c);
    else
      login(c);
  }
}

// One-letter commands over serial or telnet (port 23). Runs in the loop task;
// every command is a message to the control task, so nothing here touches hardware.
#include "console.h"
#include "../config.h"
#include "../version.h"
#include "../app/control.h"
#include "../core/log.h"
#include "../clock.h"
#include "../drivers/grove.h"
#include "report.h"
#include <WiFi.h>

static WiFiServer tserver(23);
static WiFiClient tclient;
static bool started = false;

// a peer that stops reading (a laptop gone to sleep) makes each write block for
// seconds; drop it rather than stall the loop
static void toTelnet(const String& m) {
  if (!tclient || !tclient.connected()) return;
  unsigned long t0 = millis();
  String line = m + "\r\n";
  size_t sent = tclient.write((const uint8_t*)line.c_str(), line.length());
  if (sent < line.length() || millis() - t0 > 500) {
    tclient.stop();
    Serial.println("telnet: client too slow, disconnected");
  }
}

void consoleBegin() {
  tserver.begin();
  logMirror(toTelnet);
  started = true;
}

void heartbeat() {
  static unsigned long beatAt = 0;
  if (!due(millis(), beatAt)) return;
  beatAt = millis() + 2000;
  Status s = controlStatus();
  logQuiet(String("· pir=") + String(s.pir) + " light=" + String(s.lightLevel) +
           (s.scaleOk ? " weight=" + String(s.grams, 1) + "g" : ""));
}

static void send(Cmd c, int arg = 0) {
  if (!controlSend(c, arg)) say("busy: command queue full");
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
      send(Cmd::DoorNudge, +5);
      break;  // nudge 5 deg to calibrate (mirrored scale)
    case '>':
      send(Cmd::DoorNudge, -5);
      break;
    case 'v':
      aiRequestInvoke();
      break;  // Grove commands are queued; results arrive in the log
    case 'S':
      aiRequestSnapshot();
      break;
    case 'g':
      aiRequestScan();
      break;
    case 'L':
      send(Cmd::LightOn);
      break;
    case 'l':
      send(Cmd::LightOff);
      break;
    case 'p':
      say(String("PIR now: ") + (s.pir ? "MOTION" : "quiet"));
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
      say(s.scaleOk ? "weight: " + String(s.grams, 1) + " g" : "scale not responding");
      break;
    case 'r':
      send(Cmd::RawDump);
      break;
    case 't':
      send(Cmd::Tare);
      break;
    case '?':
      say("IP " + WiFi.localIP().toString() + " | light " + String(s.lightLevel) + " | PIR " + String(s.pir) +
          " | weight " + (s.scaleOk ? String(s.grams, 1) + "g" : "n/a") + " | auto " +
          (s.autoMode ? "ON" : "off") + " | heap " + String(ESP.getFreeHeap() / 1024) + "k free, " +
          String(ESP.getMaxAllocHeap() / 1024) + "k max block | faults " + hex4(boardFaults(s)) + " " +
          bitNames(boardFaults(s), faultName) + " | tick max " + String(s.tickMaxUs) + "us, overruns " +
          String(s.overruns) + " | fw " FW_VERSION " (" FW_GIT ")");
      break;
    default:
      if (ch >= '1' && ch <= '9') send(Cmd::RotorPulse, (ch - '0') * 100);
      break;
  }
}

void consolePoll() {
  if (started && tserver.hasClient()) {
    if (tclient) tclient.stop();
    tclient = tserver.accept();
    tclient.println("=== king board " FW_VERSION ": a/A auto | f/b/s,1..9 | o/c/q | L/l | p | g | ? ===");
  }
  char ch = 0;
  if (Serial.available())
    ch = Serial.read();
  else if (tclient && tclient.connected() && tclient.available())
    ch = tclient.read();
  if (ch) command(ch);
}

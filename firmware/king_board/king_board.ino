// King board firmware (ESP32-S3).
// Recognition runs on a Grove Vision AI V2 in the camera pod; this board drives
// the rotor, door, scale, light and sensors and serves the web UI.
//
//   src/logic    the feeder state machine and dose controller: pure C++, tested on a PC
//   src/app      the control task: 10 ms tick on core 1, owns all actuators and sensors
//   src/drivers  hardware: servos, scale, light, PIR, button, indicator, grove
//   src/net      Wi-Fi, web + OTA, telnet console: runs in loop(), sends commands
//   src/core     logging, NVS store (its own worker), watchdog, OTA rollback, workers, locks
//
// Tasks:
//   control  core 1, priority 5   rotor, door, scale, light, button, PIR, feeder FSM
//   loop     core 1, priority 1   web, OTA, telnet, log output (this file's loop())
//   i2c      core 0, priority 1   Grove camera exchanges, seconds each
//   nvs      core 0, priority 1   NVS writes (milliseconds each, never in the control tick)
//
// Serial / telnet (nc <ip> 23, asks for the web password):
//   a/A auto off/on   f/b/s, 1..9 rotor   o/c/q, </> door   L/l light
//   p/T PIR   g I2C scan   v/S grove   w/r/t scale   I/i indicator   ? status

#include "src/version.h"
#include "src/config.h"
#include "src/app/control.h"
#include "src/core/log.h"
#include "src/core/rollback.h"
#include "src/core/store.h"
#include "src/core/watchdog.h"
#include "src/drivers/button.h"
#include "src/drivers/grove.h"
#include "src/drivers/indicator.h"
#include "src/drivers/light.h"
#include "src/drivers/pir.h"
#include "src/drivers/scale.h"
#include "src/drivers/servos.h"
#include "src/net/console.h"
#include "src/net/web.h"
#include "src/net/wifi.h"

void setup() {
  pinMode(PIN_ROTOR, OUTPUT);
  digitalWrite(PIN_ROTOR, LOW);  // first thing, or the servos twitch during boot
  pinMode(PIN_LID, OUTPUT);
  digitalWrite(PIN_LID, LOW);

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // with no one reading the USB port, drop output instead of blocking
  delay(USB_SETTLE_MS);      // give a serial monitor time to attach and catch the boot lines
  logBegin();
  say("King board " FW_VERSION " (" FW_GIT ", built " FW_BUILT ")");  // also lands in the web log
  sayf("last reset: %s", resetReason());
  storeBegin();
  rollbackCheck();  // may switch back to the previous firmware after repeated crashes
  storeRun();       // from here on NVS writes go to the nvs worker
  watchdogBegin(WATCHDOG_S);

  groveBegin();  // starts the i2c worker
  servosBegin();
  lightBegin();
  pirBegin();
  buttonBegin();
  indicatorBegin();
  pinMode(PIN_AUX, INPUT);
  scaleBegin();
  controlBegin();  // from here on only the control task touches the hardware above

  wifiOnFirstConnect([]() {
    consoleBegin();
    webBegin();
  });
  wifiBegin();
  watchdogSubscribe();  // the loop task, after the slow Wi-Fi start

  say("=== KING BOARD: a/A auto | f/b/s,1..9 | o/c/q | L/l | p | g | ? ===");
}

// the network task: everything here may be slow, none of it touches hardware
void loop() {
  watchdogFeed();
  logDrain();
  webPoll();
  wifiPoll();
  heartbeat();
  consolePoll();
  delay(1);
}

#pragma once
#include <stdint.h>
#include "../logic/feeder.h"
#include "../logic/health.h"

// The control task: a fixed 10 ms tick on core 1, above everything else. It owns
// the actuators and sensors, runs the feeder state machine, and is the only code
// that touches them. Other tasks talk to it with commands and read a status snapshot.
enum class Cmd : uint8_t {
  AutoOn,
  AutoOff,
  RotorFwd,
  RotorBack,
  RotorStop,
  RotorPulse,  // pulse: arg = ms
  DoorOpen,
  DoorClose,
  DoorRelease,
  DoorNudge,  // nudge: arg = degrees
  LightOn,
  LightOff,
  LightAlways,
  LightMotion,
  LightPct,  // pct: arg = 0..100
  IndicatorOn,
  IndicatorOff,
  Tare,
  RawDump,
  PirSelfTest,
};

struct Status {
  bool valid = false;  // false if the snapshot couldn't be read (lock timeout)
  bool scaleOk = false;
  float grams = 0;
  bool pir = false;
  int doorAngle = 0;
  bool doorClosed = true;
  int lightLevel = 0, lightMaxLevel = 0;  // 0..255
  bool autoMode = false;
  bool dosing = false;
  float dispensed = 0;
  feeder::Alarm alarm = feeder::Alarm::None;
  uint16_t faults = 0;     // Fault bits known to the control task (net adds WifiDown)
  uint16_t states = 0;     // State bits
  uint32_t tickMaxUs = 0;  // slowest control tick since boot
  uint32_t overruns = 0;   // ticks that overran their 10 ms slot
  uint32_t stackFree = 0;  // the control task's smallest free stack, bytes
};

void controlBegin();                     // starts the task; call at the end of setup()
bool controlSend(Cmd cmd, int arg = 0);  // from any task; false if the queue is full
Status controlStatus();                  // from any task
// OTA: the actuators go to their safe state on the next tick and every command is dropped
// until controlResume(). Not a queued command, so a full queue can't lose it. Waits up to
// HALT_ACK_MS for the control task to confirm; false if it didn't.
bool controlHalt();
void controlResume();

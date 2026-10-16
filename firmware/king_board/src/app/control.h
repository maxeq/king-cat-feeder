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
  SafeStop,
  Resume,  // around an OTA update
};

struct Status {
  bool scaleOk;
  float grams;
  bool pir;
  int doorAngle;
  bool doorClosed;
  int lightLevel, lightMaxLevel;  // 0..255
  bool autoMode;
  bool dosing;
  float dispensed;
  feeder::Alarm alarm;
  bool halted;
  uint16_t faults;     // Fault bits known to the control task (net adds WifiDown)
  uint16_t states;     // State bits
  uint32_t tickMaxUs;  // slowest control tick since boot
  uint32_t overruns;   // ticks that overran their 10 ms slot
};

void controlBegin();                     // starts the task; call at the end of setup()
bool controlSend(Cmd cmd, int arg = 0);  // from any task; false if the queue is full
Status controlStatus();                  // from any task

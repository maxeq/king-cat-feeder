#pragma once
#include <stdint.h>

// Two 16-bit words describe the whole board, like a chip's status registers:
//   faults  something is wrong; any CRITICAL bit makes the indicator blink
//   states  what is happening right now; normal, never an alarm
// One number each is easy to log, show in /status, send as telemetry and test.
// Bits are append-only: never renumber one, a dashboard may already decode it.

enum class Fault : uint16_t {
  ScaleMissing = 1 << 0,    // no HX711 found since boot
  ScaleLost = 1 << 1,       // the HX711 stopped answering
  RotorTripped = 1 << 2,    // the 20 s safety stop is latched
  GroveOffline = 1 << 3,    // the camera module doesn't answer
  NoFood = 1 << 4,          // the last dose jammed, ran dry or timed out
  DoseScaleFault = 1 << 5,  // the last dose stopped because the scale went quiet
  WifiDown = 1 << 6,
  Halted = 1 << 7,       // control is in its safe state (OTA running)
  BootCrashed = 1 << 8,  // the last reset was a crash or a watchdog
  TickOverrun = 1 << 9,  // a control tick took longer than its 10 ms slot (recently)
};

enum class State : uint16_t {
  Dosing = 1 << 0,
  DoorOpen = 1 << 1,
  DoorMoving = 1 << 2,
  ScaleTaring = 1 << 3,
  CatOnTray = 1 << 4,
  AutoMode = 1 << 5,
  LightOn = 1 << 6,
  Motion = 1 << 7,  // PIR
  CameraBusy = 1 << 8,
};

constexpr uint16_t mask(Fault f) { return static_cast<uint16_t>(f); }
constexpr uint16_t mask(State s) { return static_cast<uint16_t>(s); }
constexpr uint16_t operator|(Fault a, Fault b) { return mask(a) | mask(b); }
constexpr uint16_t operator|(uint16_t a, Fault b) { return a | mask(b); }
constexpr uint16_t operator|(State a, State b) { return mask(a) | mask(b); }
constexpr uint16_t operator|(uint16_t a, State b) { return a | mask(b); }
constexpr bool has(uint16_t word, Fault f) { return word & mask(f); }
constexpr bool has(uint16_t word, State s) { return word & mask(s); }

// faults that need a person: the indicator blinks while any of these is set
constexpr uint16_t CRITICAL_FAULTS =
    Fault::ScaleLost | Fault::RotorTripped | Fault::NoFood | Fault::DoseScaleFault | Fault::TickOverrun;

const char* faultName(int bitIndex);  // "ScaleLost", or nullptr for an unused bit
const char* stateName(int bitIndex);

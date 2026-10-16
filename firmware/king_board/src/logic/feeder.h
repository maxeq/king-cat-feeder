#pragma once
#include <stdint.h>
#include "dose.h"
#include "../clock.h"
#include "messages.h"

// The feeder's state machine. Pure C++: it reads a snapshot of the inputs each
// control tick and returns commands. It never touches hardware, so it can be
// tested on a PC.
//
//   button, door closed  -> dose (weighed with a scale, timed without), then open the door
//   button while dosing  -> cancel the dose
//   button, door open    -> close the door
//   auto mode            -> PIR motion opens the door, lights up and pulses the rotor; quiet closes
//   otherwise            -> the light follows motion, unless it is set to "always on"
namespace feeder {

enum class Alarm { None, NoFood, ScaleFault };
enum class Door { None, Open, Close, CloseAndRelease };
enum class Light { None, On, Off };
enum class Indicator { None, On, Off, Blink };

struct Inputs {
  uint32_t now = 0;
  bool buttonPressed = false;  // debounced press edge
  bool pirReady = false;       // false during the sensor's warm-up
  bool pir = false;
  bool doorClosed = true;
  bool doorMoving = false;
  bool scaleOk = false;
  float grams = 0;
  dose::Sample sample;  // fresh reading this tick, if any
  int lightLevel = 0;
};

struct Outputs {
  int rotorUs = -1;  // -1 = the FSM doesn't drive the rotor this tick
  Door door = Door::None;
  Light light = Light::None;
  Indicator indicator = Indicator::None;
  Messages log;
};

class Fsm {
 public:
  Outputs step(const Inputs& in);

  void setAuto(bool on) { auto_ = on; }
  void stopPulse() { pulseUntil_ = 0; }  // a manual stop or auto off ends an auto pulse
  bool autoMode() const { return auto_; }
  void setLightAlways(bool on, uint32_t now);
  bool lightAlways() const { return lightAlways_; }
  bool dosing() const { return dose_.running(); }
  float dispensed() const { return dose_.dispensed(); }
  bool pirMotion() const { return pirLast_; }
  bool catOnTray() const { return dose_.catOnTray(); }
  Alarm alarm() const { return alarm_; }

 private:
  void onDoseDone(dose::Result r, Outputs& o);

  dose::Controller dose_;
  bool auto_ = true, lightAlways_ = false, pirLast_ = false, timedDose_ = false;
  uint32_t pulseUntil_ = 0, blindUntil_ = 0, lightOffAt_ = 0;
  Alarm alarm_ = Alarm::None;
};

const char* alarmText(Alarm a);

}  // namespace feeder

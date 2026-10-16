#pragma once
#include <stdint.h>
#include "../types.h"

// The ports: everything the controller needs from the outside world, as small interfaces.
// The board implements each one with a driver (adapters in control.cpp); the host tests
// implement them with fakes (test/test_control.cpp). The controller never calls a driver.
// Each port is a seam on its own: a test, or a future module, takes only the ones it uses.
// Pure: only <stdint.h> and types.h, so the ports never pull hardware into the tests.

struct Rotor {                   // FS90R, continuous; the driver keeps the 20 s safety cap
  virtual bool set(int us) = 0;  // false if refused: a safety stop is latched
  virtual void clearTrip() = 0;
  virtual bool tripped() = 0;
  virtual int us() = 0;

 protected:
  ~Rotor() = default;
};

struct Door {  // MF90, positional
  virtual void moveTo(int angle, bool releaseWhenThere) = 0;
  virtual void release() = 0;
  virtual int angle() = 0;
  virtual bool closed() = 0;
  virtual bool moving() = 0;

 protected:
  ~Door() = default;
};

struct Servos {             // both servos together
  virtual void safe() = 0;  // rotor stopped and held low, door released
  virtual void tick(uint32_t now) = 0;

 protected:
  ~Servos() = default;
};

struct Scale {                                        // HX711
  virtual bool tick(uint32_t now, float& grams) = 0;  // true when a fresh reading arrived
  virtual bool ok() = 0;
  virtual ScaleHealth health() = 0;
  virtual float grams() = 0;
  virtual void tare() = 0;
  virtual void rawDump() = 0;

 protected:
  ~Scale() = default;
};

struct Light {  // the PWM light, with a soft ramp
  virtual void on() = 0;
  virtual void off() = 0;
  virtual void cut() = 0;  // off at once, no ramp
  virtual void setPct(int pct) = 0;
  virtual void tick(uint32_t now) = 0;
  virtual int level() = 0;     // current, 0..255
  virtual int maxLevel() = 0;  // the "on" level, 0..255

 protected:
  ~Light() = default;
};

struct Indicator {  // the status LED
  virtual void set(IndicatorMode m) = 0;
  virtual void tick(uint32_t now) = 0;

 protected:
  ~Indicator() = default;
};

struct Inputs {                               // button and PIR
  virtual bool buttonTick(uint32_t now) = 0;  // true once per press
  virtual void pirTick(uint32_t now) = 0;
  virtual bool pirReady(uint32_t now) = 0;
  virtual bool pir() = 0;
  virtual void pirSelfTest(uint32_t now) = 0;

 protected:
  ~Inputs() = default;
};

struct Peers {  // the rest of the board the fault word reports on
  virtual bool cameraOnline() = 0;
  virtual bool cameraBusy() = 0;
  virtual bool bootCrashed() = 0;

 protected:
  ~Peers() = default;
};

struct Store {  // settings that survive a reboot
  virtual int32_t load(Key k, int32_t fallback) = 0;
  virtual void save(Key k, int32_t value) = 0;  // never blocks

 protected:
  ~Store() = default;
};

struct Log {
  virtual void line(const char* text) = 0;  // never blocks, never allocates

 protected:
  ~Log() = default;
};

// What the controller is built with: one of each, injected through its constructor.
struct Ports {
  Rotor& rotor;
  Door& door;
  Servos& servos;
  Scale& scale;
  Light& light;
  Indicator& indicator;
  Inputs& inputs;
  Peers& peers;
  Store& store;
  Log& log;
};

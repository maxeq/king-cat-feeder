#pragma once
#include <stdint.h>
#include "../clock.h"
#include "../logic/feeder.h"
#include "control.h"
#include "ports.h"

// The control task's decisions, without the task: it applies commands, runs the feeder
// state machine, drives the hardware through its ports (ports.h) and builds the fault and state words.
// Pure C++, so the host tests run it against fake hardware; control.cpp wraps it in the
// FreeRTOS task, the command queue and the status lock.
class Controller {
 public:
  explicit Controller(const Ports& ports) : p_(ports) {}

  void begin(uint32_t now);                    // once, before the first tick
  void apply(Cmd cmd, int arg, uint32_t now);  // a command from another task
  void tick(uint32_t now);                     // every CONTROL_TICK_MS
  void tickTook(uint32_t us, uint32_t now);    // the task's measurement of the last tick

  void halt();  // OTA: safe state now, then hold until resume()
  void resume();
  bool halted() const { return halted_; }

  Status status(uint32_t now);
  uint16_t faultWord(uint32_t now);
  uint16_t stateWord();

 private:
  void log(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void rotorManual(int us, const char* what);
  bool busy();
  void applyOutputs(const feeder::Outputs& o);

  Ports p_;
  feeder::Fsm fsm_;
  bool halted_ = false;
  bool wasDosing_ = false;
  Deadline pulseStop_;                               // a console pulse ends here
  Deadline overrunHold_;                             // TickOverrun stays set until here
  IndicatorMode indicatorBase_ = IndicatorMode::On;  // what the FSM and the console asked for
  IndicatorMode indicatorShown_ = IndicatorMode::On;
  uint32_t tickMaxUs_ = 0, overruns_ = 0;
};

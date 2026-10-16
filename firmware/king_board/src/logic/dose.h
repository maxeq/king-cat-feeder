#pragma once
#include <stdint.h>
#include "../config.h"
#include "messages.h"

// Non-blocking dose controller. Pure C++, no Arduino: the control task calls step()
// every tick with the time and the latest scale sample, and gets back what the
// rotor should do. The same code runs in the unit tests on a PC.
//
// Weighed dose: loosen the kibble, ask for the door to open, then feed in cycles
// and weigh after each one. A cycle that adds less than CYCLE_MIN_G twice in a row
// escalates through anti-jam moves. A weigh with too few real samples is a scale
// fault: the rotor stops and nothing escalates. A sudden jump in weight is a cat on
// the tray, not food: the rotor pauses until the load starts to leave, then weighs again. The target only
// counts once two weighs in a row agree, because a paw changes the weight and food doesn't.
namespace dose {

enum class Result { Running, Reached, Cancelled, Jammed, Timeout, CatBlocked, ScaleFault, TimedDone };

struct Params {  // defaults from config.h; tests shorten them
  float target = DOSE_TARGET_G;
  uint32_t timeout = DOSE_TIMEOUT;
  float cycleMin = CYCLE_MIN_G;
  float catJump = CAT_JUMP_G;
  uint32_t settle = SETTLE_MS;
  uint32_t window = WEIGH_WINDOW_MS;
  uint32_t catPause = CAT_PAUSE_MS;
  float confirmTol = CONFIRM_TOL_G;
  float stepMax = STEP_MAX_G;
};

struct Sample {
  bool fresh = false;  // a new reading arrived this tick
  float grams = 0;
};

struct Output {
  int rotorUs = ROT_STOP;
  bool openDoor = false;  // true once per weighed dose, after the kibble is loosened
  Result result = Result::Running;
  Messages log;
};

class Controller {
 public:
  explicit Controller(const Params& p = Params()) : p_(p) {}
  void startWeighed(uint32_t now, float startGrams);
  void startTimed(uint32_t now);
  void cancel() { cancelRequested_ = true; }
  void abort() { phase_ = Phase::Idle; }  // stop dead, no result (the board is about to halt)
  bool running() const { return phase_ != Phase::Idle; }
  bool timed() const { return timed_; }
  bool catOnTray() const { return running() && catPresent_; }
  float dispensed() const { return lastGrams_ - startGrams_; }
  // food counted by the last full cycle weigh: a light paw that lands later doesn't move it
  float weighedDispensed() const { return prevGrams_ - startGrams_; }
  // hold = something else is shaking the tray (the door moving): weighing waits
  Output step(uint32_t now, const Sample& s, bool hold = false);

 private:
  enum class Phase { Idle, Program, Weigh, CatPause };
  enum class Program { Shake, Cycle, Timed };
  enum class After { Initial, Cycle, CatPause, Final, Confirm };  // what a weigh is for
  struct Segment {
    int us;
    uint32_t ms;
    bool watchTarget;  // stop early once the tray reaches the target
  };

  void load(const Segment* segs, int n, uint32_t now);
  void loadCycle(uint32_t now, Messages& log);
  void startWeigh(After why, uint32_t now);
  void finish(Result r, Output& out);
  void onWeighed(float w, uint32_t now, Output& out);
  void evaluate(float w, float d, uint32_t now, Output& out);
  bool reached(float grams) const { return grams - startGrams_ >= p_.target - TARGET_TOL_G; }
  void pauseForCat(float jump, float level, uint32_t now, Output& out);
  void confirm(float w, uint32_t now);
  bool scaleQuiet(uint32_t now) const { return now - lastFresh_ > p_.window; }

  Params p_;

  Phase phase_ = Phase::Idle;
  Program program_ = Program::Shake;
  bool timed_ = false, cancelRequested_ = false;
  bool catPresent_ = false;           // the last weigh saw a jump: the cat (or a paw) is on the tray
  Result pending_ = Result::Running;  // result decided, waiting for the final weigh
  uint32_t t0_ = 0, phaseStart_ = 0;
  float startGrams_ = 0, prevGrams_ = 0, lastGrams_ = 0, confirmGrams_ = 0;
  float lastSample_ = 0;
  float catLevel_ = 0;     // the heaviest reading since something stepped on
  float catLeave_ = 0;     // a drop this big from catLevel_ means it is leaving
  bool stepSeen_ = false;  // a jump during a weigh that only reports
  bool haveSample_ = false;
  uint32_t lastFresh_ = 0;  // when the scale last answered
  int level_ = 0, emptyCycles_ = 0;

  static const int MAX_SEGMENTS = 16;
  Segment prog_[MAX_SEGMENTS] = {};
  int progLen_ = 0, seg_ = 0;
  uint32_t segStart_ = 0;

  After weighFor_ = After::Initial;
  float samples_[WEIGH_SAMPLES] = {};
  int nSamples_ = 0;
};

const char* resultName(Result r);

}  // namespace dose

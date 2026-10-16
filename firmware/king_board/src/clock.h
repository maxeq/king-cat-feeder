#pragma once
#include <stdint.h>

// millis() wraps every ~49.7 days. Compare deadlines by signed difference, never by
// `now > deadline`, so a deadline set just before the wrap still fires on time.
// A signed difference only reaches ~24.8 days, so a deadline that has fired must be
// cleared (0 = none) rather than left behind: an old one would read "not yet" again.
inline bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

// Time since `since`, for durations. Unsigned subtraction already survives the wrap; this
// also reads a `since` stamped a moment after `now` (a driver that calls millis() itself,
// later in the same tick) as 0 instead of ~49.7 days.
inline uint32_t elapsed(uint32_t now, uint32_t since) {
  int32_t d = (int32_t)(now - since);
  return d > 0 ? (uint32_t)d : 0;
}

// A one-shot timer that follows the rules above, so drivers don't repeat them by hand.
// start() arms it; expired() is true once, on the first call at or after the deadline,
// and disarms it. Tested across both wraps in test/test_control.cpp.
// Poll it every tick (or re-arm it before each use): like due(), an armed deadline that
// nobody checks for 2^31 ms (~24.8 days) reads "not yet" again. A check hidden behind
// `a || d.running(now)` is not a poll: the || may skip it.
class Deadline {
 public:
  void start(uint32_t now, uint32_t ms) { at_ = (now + ms) | 1; }  // | 1: 0 means "not armed"
  void stop() { at_ = 0; }
  bool armed() const { return at_ != 0; }
  bool expired(uint32_t now) {
    if (!at_ || !due(now, at_)) return false;
    at_ = 0;
    return true;
  }
  bool running(uint32_t now) {  // armed and not yet due
    expired(now);
    return armed();
  }

 private:
  uint32_t at_ = 0;
};

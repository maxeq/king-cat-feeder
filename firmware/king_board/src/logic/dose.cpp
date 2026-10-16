#include "dose.h"
#include "../config.h"

namespace dose {

const char* resultName(Result r) {
  switch (r) {
    case Result::Running:
      return "running";
    case Result::Reached:
      return "reached";
    case Result::Cancelled:
      return "cancelled";
    case Result::Jammed:
      return "jammed";
    case Result::Timeout:
      return "timeout";
    case Result::CatBlocked:
      return "cat blocked";
    case Result::ScaleFault:
      return "scale fault";
    case Result::TimedDone:
      return "timed";
  }
  return "?";
}

void Controller::load(const Segment* segs, int n, uint32_t now) {
  for (int i = 0; i < n && i < MAX_SEGMENTS; i++)
    prog_[i] = segs[i];
  progLen_ = n < MAX_SEGMENTS ? n : MAX_SEGMENTS;
  seg_ = 0;
  segStart_ = now;
  phase_ = Phase::Program;
}

void Controller::startWeighed(uint32_t now, float startGrams) {
  timed_ = false;
  cancelRequested_ = false;
  catPresent_ = false;
  haveSample_ = false;
  lastFresh_ = now;
  pending_ = Result::Running;
  t0_ = now;
  startGrams_ = prevGrams_ = lastGrams_ = startGrams;
  level_ = emptyCycles_ = 0;
  // loosen the kibble before the first weigh
  static const Segment shake[] = {
      {ROT_FWD, SHAKE_MS, false}, {ROT_BACK, SHAKE_MS, false}, {ROT_FWD, SHAKE_MS, false}};
  program_ = Program::Shake;
  load(shake, 3, now);
}

void Controller::startTimed(uint32_t now) {
  timed_ = true;
  cancelRequested_ = false;
  lastFresh_ = now;
  pending_ = Result::Running;
  t0_ = now;
  startGrams_ = prevGrams_ = lastGrams_ = 0;
  static const Segment timed[] = {{ROT_BACK, TIMED_KNOCK_MS, false},
                                  {ROT_FWD, TIMED_KNOCK_MS, false},
                                  {ROT_FWD, TIMED_RUN_MS, false},
                                  {ROT_BACK, TIMED_RUN_MS, false}};
  program_ = Program::Timed;
  load(timed, 4, now);
}

void Controller::loadCycle(uint32_t now, Messages& log) {
  program_ = Program::Cycle;
  switch (level_) {
    case 0: {  // normal: unload, then a slow feed
      static const Segment s[] = {{ROT_BACK, UNLOAD_MS, true}, {ROT_FWD_SLOW, FEED_SLOW_MS, true}};
      load(s, 2, now);
      break;
    }
    case 1: {
      log.add("  escalation 1: rocking");
      static const Segment s[] = {{ROT_BACK, ROCK_BACK_MS, false},
                                  {ROT_FWD, ROCK_FWD_MS, true},
                                  {ROT_BACK, ROCK_BACK_MS, false},
                                  {ROT_FWD, ROCK_FWD_MS, true},
                                  {ROT_FWD_SLOW, PUSH_SLOW_MS, true}};
      load(s, 5, now);
      break;
    }
    case 2: {
      log.add("  escalation 2: jackhammer");
      Segment s[2 * HAMMER_KNOCKS + 2];
      for (size_t i = 0; i < (size_t)HAMMER_KNOCKS; i++) {
        s[2 * i] = {ROT_FWD, HAMMER_MS, false};
        s[2 * i + 1] = {ROT_BACK, HAMMER_MS, false};
      }
      const size_t end = 2 * (size_t)HAMMER_KNOCKS;
      s[end] = {ROT_BACK, ROCK_BACK_MS, false};
      s[end + 1] = {ROT_FWD_SLOW, PUSH_SLOW_MS, true};
      load(s, 2 * HAMMER_KNOCKS + 2, now);
      break;
    }
    default: {
      log.add("  escalation 3: long reverse + push through");
      Segment s[2 * KNOCKS + 3];
      s[0] = {ROT_BACK, LONG_REVERSE_MS, false};
      for (size_t i = 0; i < (size_t)KNOCKS; i++) {
        s[1 + 2 * i] = {ROT_FWD, KNOCK_MS, false};
        s[2 + 2 * i] = {ROT_BACK, KNOCK_MS, false};
      }
      s[2 * KNOCKS + 1] = {ROT_FWD, ROCK_FWD_MS, true};
      s[2 * KNOCKS + 2] = {ROT_FWD_SLOW, PUSH_SLOW_MS, true};
      load(s, 2 * KNOCKS + 3, now);
      break;
    }
  }
}

void Controller::startWeigh(After why, uint32_t now) {
  stepSeen_ = false;
  phase_ = Phase::Weigh;
  weighFor_ = why;
  phaseStart_ = now;
  nSamples_ = 0;
}

static float median(float* v, int n) {
  for (int i = 1; i < n; i++) {
    float k = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > k) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = k;
  }
  return v[n / 2];
}

void Controller::finish(Result r, Output& out) {
  phase_ = Phase::Idle;
  out.result = r;
  out.rotorUs = ROT_STOP;
  float got = lastGrams_ - startGrams_;
  switch (r) {
    case Result::Cancelled:
      out.log.add("dose: CANCELLED by button (%.1f g)", got);
      break;
    case Result::ScaleFault:
      out.log.add("dose: SCALE FAULT — rotor stopped, no escalation (%.1f g)", got);
      break;
    case Result::Reached:
      out.log.add("dose: TARGET REACHED (%.1f g)", got);
      break;
    case Result::Jammed:
      out.log.add("dose: JAM/EMPTY — ladder exhausted (%.1f g)", got);
      break;
    case Result::Timeout:
      out.log.add("dose: TIMEOUT %lus (%.1f g)", (unsigned long)(p_.timeout / 1000), got);
      break;
    case Result::CatBlocked:
      out.log.add("dose: STOPPED — something stayed on the tray (%.1f g)", prevGrams_ - startGrams_);
      break;
    default:
      break;
  }
}

// The rotor is stopped during a pause, so no food arrives: any real drop in weight
// means the load is leaving. The pause ends on a drop from the heaviest reading, and a
// fresh weigh decides whether to feed on or keep waiting. No guess at "the weight
// before the cat" is needed, which a load that creeps on would get wrong.
void Controller::pauseForCat(float jump, float level, uint32_t now, Output& out) {
  if (pending_ == Result::Reached) pending_ = Result::Running;  // the "target" was the cat
  if (!catPresent_) {
    out.log.add("  scale: jump +%.1f g — object on tray? pause until it leaves", jump);
    catLevel_ = level;
    // scale noise at rest is ~0.2 g, so a light paw can be caught leaving by a small drop
    float leave = CAT_LEAVE_SHARE * jump;
    catLeave_ = leave < CAT_LEAVE_MIN_G ? CAT_LEAVE_MIN_G : leave > p_.stepMax / 2 ? p_.stepMax / 2 : leave;
  } else if (level > catLevel_) {
    catLevel_ = level;
  }
  catPresent_ = true;
  phase_ = Phase::CatPause;
  phaseStart_ = now;
}

// the tray looks full: weigh once more before believing it
void Controller::confirm(float w, uint32_t now) {
  pending_ = Result::Running;
  confirmGrams_ = w;
  startWeigh(After::Confirm, now);
}

void Controller::evaluate(float w, float d, uint32_t now, Output& out) {
  catPresent_ = false;
  if (reached(w)) {
    confirm(w, now);
    return;
  }
  if (d >= p_.cycleMin) {
    level_ = 0;
    emptyCycles_ = 0;
  } else if (++emptyCycles_ >= EMPTY_CYCLES_TO_ESCALATE) {
    emptyCycles_ = 0;
    if (++level_ > MAX_ESCALATION) {
      finish(Result::Jammed, out);
      return;
    }
  }
  prevGrams_ = w;
  out.log.add("  dispensed %.1f g (cycle +%.1f, level %d)", w - startGrams_, d, level_);
  loadCycle(now, out.log);
}

void Controller::onWeighed(float w, uint32_t now, Output& out) {
  lastGrams_ = w;
  switch (weighFor_) {
    case After::Initial:
      prevGrams_ = w;
      loadCycle(now, out.log);
      break;
    case After::Final:
      if (pending_ == Result::Reached && w - prevGrams_ > p_.catJump) {  // the "target" was a cat
        pauseForCat(w - prevGrams_, w, now, out);
        break;
      }
      if (pending_ == Result::Timeout && reached(w) && !stepSeen_ &&
          w - prevGrams_ <= p_.catJump) {  // it got there on the last cycle after all
        finish(Result::Reached, out);
        break;
      }
      if (pending_ == Result::Reached) {  // a single reading crossed the target mid-feed
        if (reached(w))
          confirm(w, now);
        else {  // a paw tipped one reading over: keep feeding
          pending_ = Result::Running;
          evaluate(w, w - prevGrams_, now, out);
        }
        break;
      }
      finish(pending_, out);
      break;
    case After::Confirm: {
      float drift = w - confirmGrams_;
      if (drift > p_.confirmTol)  // still climbing: something is stepping on
        pauseForCat(drift, w, now, out);
      else if (drift < -p_.confirmTol || !reached(w))  // it was partly a paw, now gone
        evaluate(w, w - prevGrams_, now, out);
      else
        finish(Result::Reached, out);
      break;
    }
    case After::Cycle:
    case After::CatPause: {  // still heavy after a pause = the cat is still there, keep waiting
      float d = w - prevGrams_;
      if (d > p_.catJump)  // came on slowly; the food share is unknown, so wait for the cat's share to leave
        pauseForCat(d, w, now, out);
      else
        evaluate(w, d, now, out);
      break;
    }
  }
}

Output Controller::step(uint32_t now, const Sample& s, bool hold) {
  Output out;
  out.rotorUs = ROT_STOP;
  if (phase_ == Phase::Idle) return out;
  if (s.fresh) {
    lastGrams_ = s.grams;
    bool shaking = phase_ == Phase::Program && program_ == Program::Shake;          // knocks are expected
    bool reporting = pending_ == Result::Cancelled || pending_ == Result::Timeout;  // final weigh only
    bool watch = !timed_ && !shaking && !hold && !reporting && !catPresent_;
    if (haveSample_ && reporting && s.grams - lastSample_ > p_.stepMax) stepSeen_ = true;
    if (haveSample_ && watch && s.grams - lastSample_ > p_.stepMax)
      pauseForCat(s.grams - lastSample_, s.grams, now, out);  // too fast to be kibble
    lastSample_ = s.grams;
    haveSample_ = true;
    lastFresh_ = now;
  }

  if (cancelRequested_ && pending_ == Result::Running) {
    cancelRequested_ = false;
    if (timed_) {
      finish(Result::Cancelled, out);
      return out;
    }
    pending_ = Result::Cancelled;
    startWeigh(After::Final, now);
  }
  if (now - t0_ >= p_.timeout + p_.settle + p_.window) {  // backstop: never outlive the timeout
    finish(catPresent_ ? Result::CatBlocked : scaleQuiet(now) ? Result::ScaleFault : Result::Timeout, out);
    return out;
  }
  bool confirming = phase_ == Phase::Weigh && weighFor_ == After::Confirm;  // let a confirm finish
  if (!timed_ && !confirming && pending_ == Result::Running && now - t0_ >= p_.timeout) {
    if (catPresent_) {  // a final weigh would only weigh the cat
      finish(Result::CatBlocked, out);
      return out;
    }
    pending_ = Result::Timeout;
    startWeigh(After::Final, now);
  }

  switch (phase_) {
    case Phase::Program: {
      if (!timed_ && program_ != Program::Shake && s.fresh && s.grams - prevGrams_ > p_.catJump) {
        pauseForCat(s.grams - prevGrams_, s.grams, now, out);  // too much for kibble
        break;
      }
      if (prog_[seg_].watchTarget && s.fresh && reached(s.grams)) {
        pending_ = Result::Reached;
        startWeigh(After::Final, now);
        break;
      }
      if (now - segStart_ >= prog_[seg_].ms) {
        seg_++;
        segStart_ = now;
        if (seg_ >= progLen_) {
          if (program_ == Program::Timed) {
            finish(Result::TimedDone, out);
            return out;
          }
          if (program_ == Program::Shake) out.openDoor = true;
          startWeigh(program_ == Program::Shake ? After::Initial : After::Cycle, now);
          break;
        }
      }
      out.rotorUs = prog_[seg_].us;
      break;
    }
    case Phase::Weigh: {  // rotor stopped: settle, then collect up to WEIGH_SAMPLES samples
      if (hold) {         // the tray is shaking: start settling over
        phaseStart_ = now;
        nSamples_ = 0;
      }
      uint32_t t = now - phaseStart_;
      if (t < p_.settle) break;
      if (s.fresh && nSamples_ < WEIGH_SAMPLES) samples_[nSamples_++] = s.grams;
      if (nSamples_ < WEIGH_SAMPLES && t < p_.settle + p_.window) break;
      if (nSamples_ >= WEIGH_MIN_SAMPLES) {
        float lo = samples_[0], hi = samples_[0];
        for (int i = 1; i < nSamples_; i++) {
          lo = samples_[i] < lo ? samples_[i] : lo;
          hi = samples_[i] > hi ? samples_[i] : hi;
        }
        bool reporting = pending_ == Result::Cancelled || pending_ == Result::Timeout;
        if (hi - lo > p_.stepMax / 2) {  // rotor stopped yet the weight moved: something on the tray
          if (reporting) {
            stepSeen_ = true;
          } else {
            pauseForCat(hi - lo, hi, now, out);
            break;
          }
        }
        onWeighed(median(samples_, nSamples_), now, out);
      } else if (weighFor_ == After::Final && pending_ == Result::Cancelled)
        finish(pending_, out);  // the outcome is known; report the last reading
      else  // a silent scale can't confirm a target or a timeout: a scale fault, evidence first
        finish(Result::ScaleFault, out);
      break;
    }
    case Phase::CatPause:     // wait for the load to start leaving, then weigh again
      if (scaleQuiet(now)) {  // the scale went quiet: we'd wait for the cat forever
        finish(Result::ScaleFault, out);
        return out;
      }
      if (s.fresh && s.grams > catLevel_) catLevel_ = s.grams;  // more paws coming on
      if (now - phaseStart_ >= p_.catPause && s.fresh && s.grams < catLevel_ - catLeave_) {
        out.log.add("  scale: tray clear again, carrying on");
        catPresent_ = false;
        startWeigh(After::CatPause, now);
      }
      break;
    case Phase::Idle:
      break;
  }
  if (phase_ == Phase::Program) out.rotorUs = prog_[seg_].us;
  return out;
}

}  // namespace dose

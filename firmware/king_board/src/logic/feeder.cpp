#include "feeder.h"
#include "../clock.h"
#include "../config.h"

namespace feeder {

const char* alarmText(Alarm a) {
  switch (a) {
    case Alarm::NoFood:
      return "food not flowing, check the hopper";
    case Alarm::ScaleFault:
      return "scale not answering, check the load cell";
    case Alarm::SlowFlow:
      return "food flowing too slowly, check the hopper";
    default:
      return "";
  }
}

void Fsm::setLightAlways(bool on, uint32_t now) {
  lightAlways_ = on;
  if (!on) lightOffAt_ = (now + MOTION_LIGHT_MS) | 1;  // fall back to "on motion" without a sudden off
}

void Fsm::abort() {
  dose_.abort();
  pulseUntil_ = 0;
}

void Fsm::onDoseDone(dose::Result r, Outputs& o) {
  using dose::Result;
  if (r == Result::ScaleFault)
    alarm_ = Alarm::ScaleFault;
  else if (r == Result::Jammed)
    alarm_ = Alarm::NoFood;
  else if (r == Result::Timeout)  // slow flow needs food to have flowed; none at all is no food
    alarm_ = dose_.weighedDispensed() >= CYCLE_MIN_G ? Alarm::SlowFlow : Alarm::NoFood;
  else if (timedDose_ && alarm_ == Alarm::ScaleFault)
    ;  // a timed dose (done or cancelled) ran because the scale is still dead: keep saying so
  else
    alarm_ = Alarm::None;  // reached, cancelled, a cat in the way, or a timed dose

  if (alarm_ != Alarm::None) {
    o.indicator = Indicator::Blink;
    o.log.add("!!! ALARM: %s (indicator blinking)", alarmText(alarm_));
  } else {
    o.indicator = Indicator::On;
  }
  o.rotorUs = ROT_STOP;
  o.door = Door::Open;
}

Outputs Fsm::step(const Inputs& in) {
  Outputs o;

  if (in.buttonPressed) {
    if (dose_.running()) {
      dose_.cancel();
    } else if (in.doorMoving) {
      o.log.add("BUTTON: door still moving, press again when it stops");
    } else if (in.doorClosed) {
      if (in.scaleOk) {
        o.log.add("BUTTON -> weighed dose: target +%.0f g (start %.1f g)", DOSE_TARGET_G, in.grams);
        dose_.startWeighed(in.now, in.grams);
      } else {
        o.log.add("BUTTON -> timed dose (scale n/a): shake+%lus+%lus", (unsigned long)(TIMED_RUN_MS / 1000),
                  (unsigned long)(TIMED_RUN_MS / 1000));
        dose_.startTimed(in.now);
      }
      pulseUntil_ = 0;  // a dose owns the rotor
    } else {
      o.log.add("BUTTON -> door: closing");
      o.door = Door::CloseAndRelease;
    }
  }

  if (dose_.running()) {
    dose::Output d = dose_.step(in.now, in.sample, in.doorMoving);
    o.rotorUs = d.rotorUs;
    o.log.append(d.log);
    if (d.openDoor) {
      o.door = Door::Open;
      o.log.add("door: opening (cat doesn't wait)");
    }
    timedDose_ = dose_.timed();
    if (d.result != dose::Result::Running) onDoseDone(d.result, o);
    blindUntil_ = ((in.now + PIR_BLIND_MS) | 1);  // rotor vibration trips the PIR: ignore it while dosing
  }

  if (pulseUntil_ && due(in.now, pulseUntil_)) {
    pulseUntil_ = 0;
    blindUntil_ = ((in.now + PIR_BLIND_MS) | 1);
    o.rotorUs = ROT_STOP;
    o.log.add("auto: rotor stop (pulse), PIR pause %.1fs", PIR_BLIND_MS / 1000.0);
  } else if (pulseUntil_) {
    o.rotorUs = ROT_FWD;
  }

  if (blindUntil_ && due(in.now, blindUntil_)) blindUntil_ = 0;
  if (!in.pirReady || blindUntil_) return o;

  if (in.pir != pirLast_) {
    pirLast_ = in.pir;
    o.log.add("PIR: %s", in.pir ? "MOTION" : "quiet");
    if (auto_) {
      if (in.pir) {
        o.log.add("auto: LIGHT + DOOR + rotor %lus", (unsigned long)(AUTO_PULSE_MS / 1000));
        o.light = Light::On;
        o.door = Door::Open;
        pulseUntil_ = ((in.now + AUTO_PULSE_MS) | 1);
        o.rotorUs = ROT_FWD;
      } else {
        o.log.add("auto: door close, light off");
        o.light = Light::Off;
        o.door = Door::CloseAndRelease;
      }
    }
  }

  if (!auto_ && !lightAlways_) {  // motion light: on with motion, off a while after it stops
    if (in.pir) {
      o.light = Light::On;  // idempotent; also catches motion while the light is dimming
      lightOffAt_ = ((in.now + MOTION_LIGHT_MS) | 1);
    } else if (lightOffAt_ && due(in.now, lightOffAt_)) {
      o.light = Light::Off;
      lightOffAt_ = 0;
    }
  }
  return o;
}

}  // namespace feeder

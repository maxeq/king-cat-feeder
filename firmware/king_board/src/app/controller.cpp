#include "controller.h"
#include <stdarg.h>
#include <stdio.h>
#include "../config.h"

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void Controller::log(const char* fmt, ...) {
  char line[128];
  va_list ap;  // NOLINT(cppcoreguidelines-init-variables): va_start sets it
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  p_.log.line(line);
}

void Controller::begin(uint32_t now) {
  // Auto mode always starts off: a reboot (power cut, crash, OTA) must never start feeding
  // or open the door on its own. The light mode is only a preference, so it is restored.
  fsm_.setAuto(false);
  if (p_.store.load(Key::LightAlways, 0)) {
    fsm_.setLightAlways(true, now);
    p_.light.on();
  }
  if (p_.store.load(Key::DoseRunning, 0)) {
    log("boot: a dose was running when the board restarted; it is not resumed");
    p_.store.save(Key::DoseRunning, 0);
  }
  p_.indicator.set(IndicatorMode::On);
}

void Controller::rotorManual(int us, const char* what) {
  if (p_.rotor.set(us))
    log("rotor: %s", what);
  else
    log("rotor: refused, safety stop latched (send s to re-arm)");
}

bool Controller::busy() {  // manual motor commands wait until a dose is over
  if (!fsm_.dosing()) return false;
  log("busy: dosing (press the button to cancel)");
  return true;
}

void Controller::apply(Cmd cmd, int arg, uint32_t now) {
  if (halted_) {  // nothing may re-arm an actuator while the board is in its safe state
    log("halted: command ignored until OTA ends");
    return;
  }
  switch (cmd) {
    case Cmd::AutoOn:
      fsm_.setAuto(true);
      log("auto: ON");
      break;
    case Cmd::AutoOff:
      fsm_.setAuto(false);
      fsm_.stopPulse();
      log("auto: off");
      if (!fsm_.dosing()) p_.rotor.set(ROT_STOP);
      p_.light.off();
      break;
    case Cmd::RotorFwd:
      if (!busy()) rotorManual(ROT_FWD, "FORWARD");
      break;
    case Cmd::RotorBack:
      if (!busy()) rotorManual(ROT_BACK, "BACK");
      break;
    case Cmd::RotorStop:
      if (busy()) break;
      p_.rotor.clearTrip();  // an operator stop re-arms a latched safety stop
      fsm_.stopPulse();
      pulseStop_.stop();
      rotorManual(ROT_STOP, "STOP");
      break;
    case Cmd::RotorPulse: {
      if (busy()) break;
      char what[32];
      snprintf(what, sizeof(what), "pulse %d ms", arg);
      rotorManual(ROT_FWD, what);
      if (p_.rotor.us() == ROT_FWD) pulseStop_.start(now, (uint32_t)(arg > 0 ? arg : 0));
      break;
    }
    case Cmd::DoorOpen:
      if (!busy()) {
        log("door: opening...");
        p_.door.moveTo(ANG_OPEN, false);
      }
      break;
    case Cmd::DoorClose:
      if (!busy()) {
        log("door: closing...");
        p_.door.moveTo(ANG_CLOSED, false);
      }
      break;
    case Cmd::DoorRelease:
      p_.door.release();
      log("door: detach+LOW");
      break;
    case Cmd::DoorNudge:
      if (!busy()) p_.door.moveTo(clampInt(p_.door.angle() + arg, ANG_NUDGE_MIN, ANG_NUDGE_MAX), false);
      break;
    case Cmd::LightOn:
      log("light: ON");
      p_.light.on();
      break;
    case Cmd::LightOff:
      log("light: off");
      p_.light.off();
      break;
    case Cmd::LightAlways:
      fsm_.setLightAlways(true, now);
      p_.store.save(Key::LightAlways, 1);
      p_.light.on();
      log("web: light ALWAYS");
      break;
    case Cmd::LightMotion:
      fsm_.setLightAlways(false, now);
      p_.store.save(Key::LightAlways, 0);
      log("web: light on motion");
      break;
    case Cmd::LightPct:
      p_.light.setPct(clampInt(arg, 0, 100));
      log("web: brightness %d%%", clampInt(arg, 0, 100));
      break;
    case Cmd::IndicatorOn:
      indicatorBase_ = IndicatorMode::On;
      log("indicator: ON");
      break;
    case Cmd::IndicatorOff:
      indicatorBase_ = IndicatorMode::Off;
      log("indicator: off");
      break;
    case Cmd::Tare:
      if (!busy()) p_.scale.tare();
      break;
    case Cmd::RawDump:
      p_.scale.rawDump();
      break;
    case Cmd::PirSelfTest:
      p_.inputs.pirSelfTest(now);
      break;
  }
}

void Controller::halt() {
  if (halted_) return;
  if (fsm_.dosing()) {
    log("dose: aborted, the board is halting");
    p_.store.save(Key::DoseRunning, 0);  // not interrupted by a crash: no report on the next boot
    wasDosing_ = false;
  }
  fsm_.abort();
  pulseStop_.stop();
  p_.servos.safe();
  p_.light.cut();
  halted_ = true;
  log("halted: actuators in their safe state");
}

void Controller::resume() {
  if (!halted_) return;
  halted_ = false;
  log("resumed");
}

void Controller::applyOutputs(const feeder::Outputs& o) {
  for (int i = 0; i < o.log.count; i++)
    p_.log.line(o.log.line[i]);
  if (o.rotorUs >= 0) p_.rotor.set(o.rotorUs);
  switch (o.door) {
    case feeder::Door::Open:
      p_.door.moveTo(ANG_OPEN, false);
      break;
    case feeder::Door::Close:
      p_.door.moveTo(ANG_CLOSED, false);
      break;
    case feeder::Door::CloseAndRelease:
      p_.door.moveTo(ANG_CLOSED, true);
      break;
    case feeder::Door::None:
      break;
  }
  if (o.light == feeder::Light::On) p_.light.on();
  if (o.light == feeder::Light::Off) p_.light.off();
  // the FSM says on/off; blinking comes from the fault word (any critical bit)
  if (o.indicator == feeder::Indicator::On) indicatorBase_ = IndicatorMode::On;
  if (o.indicator == feeder::Indicator::Off) indicatorBase_ = IndicatorMode::Off;
}

void Controller::tick(uint32_t now) {
  if (halted_) return;

  float grams = 0;
  bool fresh = p_.scale.tick(now, grams);
  p_.inputs.pirTick(now);

  feeder::Inputs in;
  in.now = now;
  in.buttonPressed = p_.inputs.buttonTick(now);
  if (in.buttonPressed) p_.rotor.clearTrip();  // pressing the button is an operator action too
  in.pirReady = p_.inputs.pirReady(now);
  in.pir = p_.inputs.pir();
  in.doorClosed = p_.door.closed();
  in.doorMoving = p_.door.moving();
  in.scaleOk = p_.scale.ok();
  in.grams = p_.scale.grams();
  in.sample.fresh = fresh;
  in.sample.grams = grams;
  applyOutputs(fsm_.step(in));

  bool dosing = fsm_.dosing();
  if (dosing != wasDosing_) {  // remembered, so a reboot mid-dose is reported on the next boot
    p_.store.save(Key::DoseRunning, dosing ? 1 : 0);
    wasDosing_ = dosing;
  }
  if (dosing) pulseStop_.stop();  // the dose took over the rotor
  if (pulseStop_.expired(now)) {
    p_.rotor.set(ROT_STOP);
    log("rotor: STOP");
  }
  p_.servos.tick(now);
  p_.light.tick(now);
  IndicatorMode want = (faultWord(now) & CRITICAL_FAULTS) ? IndicatorMode::Blink : indicatorBase_;
  if (want != indicatorShown_) p_.indicator.set(indicatorShown_ = want);
  p_.indicator.tick(now);
}

void Controller::tickTook(uint32_t us, uint32_t now) {
  if (us > tickMaxUs_) tickMaxUs_ = us;
  if (us > CONTROL_TICK_MS * 1000) {  // the tick overran its slot: timing can't be trusted
    overruns_++;
    overrunHold_.start(now, TICK_OVERRUN_HOLD_MS);
  }
}

uint16_t Controller::faultWord(uint32_t now) {
  uint16_t f = 0;
  ScaleHealth sh = p_.scale.health();
  if (sh == ScaleHealth::Missing) f = f | Fault::ScaleMissing;
  if (sh == ScaleHealth::Lost) f = f | Fault::ScaleLost;
  if (p_.rotor.tripped()) f = f | Fault::RotorTripped;
  if (!p_.peers.cameraOnline()) f = f | Fault::GroveOffline;
  if (fsm_.alarm() == feeder::Alarm::NoFood) f = f | Fault::NoFood;
  if (fsm_.alarm() == feeder::Alarm::ScaleFault) f = f | Fault::DoseScaleFault;
  if (fsm_.alarm() == feeder::Alarm::SlowFlow) f = f | Fault::DoseTimeout;
  if (halted_) f = f | Fault::Halted;
  if (p_.peers.bootCrashed()) f = f | Fault::BootCrashed;
  if (overrunHold_.running(now)) f = f | Fault::TickOverrun;
  return f;
}

uint16_t Controller::stateWord() {
  uint16_t st = 0;
  if (fsm_.dosing()) st = st | State::Dosing;
  if (!p_.door.closed()) st = st | State::DoorOpen;
  if (p_.door.moving()) st = st | State::DoorMoving;
  if (p_.scale.health() == ScaleHealth::Taring) st = st | State::ScaleTaring;
  if (fsm_.catOnTray()) st = st | State::CatOnTray;
  if (fsm_.autoMode()) st = st | State::AutoMode;
  if (p_.light.level() > 0) st = st | State::LightOn;
  if (fsm_.pirMotion()) st = st | State::Motion;
  if (p_.peers.cameraBusy()) st = st | State::CameraBusy;
  return st;
}

Status Controller::status(uint32_t now) {
  Status s;
  s.valid = true;
  s.scaleOk = p_.scale.ok();
  s.grams = p_.scale.grams();
  s.pir = fsm_.pirMotion();
  s.doorAngle = p_.door.angle();
  s.doorClosed = p_.door.closed();
  s.lightLevel = p_.light.level();
  s.lightMaxLevel = p_.light.maxLevel();
  s.autoMode = fsm_.autoMode();
  s.dosing = fsm_.dosing();
  s.dispensed = fsm_.dispensed();
  s.alarm = fsm_.alarm();
  s.faults = faultWord(now);
  s.states = stateWord();
  s.tickMaxUs = tickMaxUs_;
  s.overruns = overruns_;
  return s;
}

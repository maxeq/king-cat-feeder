#include "control.h"
#include "../config.h"
#include "../core/lock.h"
#include "../core/log.h"
#include "../core/rollback.h"
#include "../core/watchdog.h"
#include "../clock.h"
#include "../drivers/button.h"
#include "../drivers/grove.h"
#include "../drivers/indicator.h"
#include "../drivers/light.h"
#include "../drivers/pir.h"
#include "../drivers/scale.h"
#include "../drivers/servos.h"
#include <Arduino.h>

struct Command {
  Cmd cmd;
  int arg;
};

static QueueHandle_t commands = nullptr;
static feeder::Fsm fsm;
static Mutex statusLock;
static Status status;  // guarded by statusLock
static bool halted = false;
static uint32_t pulseStopAt = 0;
static IndicatorMode indicatorBase = IndicatorMode::On;  // what the FSM and the console asked for
static IndicatorMode indicatorShown = IndicatorMode::On;
static uint32_t tickMaxUs = 0, overruns = 0, lastOverrun = 0;

bool controlSend(Cmd cmd, int arg) {
  Command c{cmd, arg};
  return commands && xQueueSend(commands, &c, 0) == pdTRUE;
}

Status controlStatus() {
  Locked lock(statusLock);
  return status;
}

static void rotorManual(int us, const char* what) {
  if (rotorSet(us))
    say(String("rotor: ") + what);
  else
    say("rotor: refused, safety stop latched (send s to re-arm)");
}

static bool busy() {  // manual motor commands wait until a dose is over
  if (!fsm.dosing()) return false;
  say("busy: dosing (press the button to cancel)");
  return true;
}

static void apply(const Command& c, uint32_t now) {
  switch (c.cmd) {
    case Cmd::AutoOn:
      fsm.setAuto(true);
      say("auto: ON");
      break;
    case Cmd::AutoOff:
      fsm.setAuto(false);
      fsm.stopPulse();
      say("auto: off");
      if (!fsm.dosing()) rotorSet(ROT_STOP);
      lightOff();
      break;
    case Cmd::RotorFwd:
      if (!busy()) rotorManual(ROT_FWD, "FORWARD");
      break;
    case Cmd::RotorBack:
      if (!busy()) rotorManual(ROT_BACK, "BACK");
      break;
    case Cmd::RotorStop:
      if (busy()) break;
      rotorClearTrip();  // an operator stop re-arms a latched safety stop
      fsm.stopPulse();
      pulseStopAt = 0;
      rotorManual(ROT_STOP, "STOP");
      break;
    case Cmd::RotorPulse:
      if (busy()) break;
      rotorManual(ROT_FWD, ("pulse " + String(c.arg) + " ms").c_str());
      if (rotorUs() == ROT_FWD) pulseStopAt = (now + c.arg) | 1;  // 0 means no pulse
      break;
    case Cmd::DoorOpen:
      if (!busy()) {
        say("door: opening...");
        doorMoveTo(ANG_OPEN);
      }
      break;
    case Cmd::DoorClose:
      if (!busy()) {
        say("door: closing...");
        doorMoveTo(ANG_CLOSED);
      }
      break;
    case Cmd::DoorRelease:
      doorRelease();
      say("door: detach+LOW");
      break;
    case Cmd::DoorNudge:
      if (!busy()) doorMoveTo(constrain(doorAngle() + c.arg, 40, 180));
      break;
    case Cmd::LightOn:
      say("light: ON");
      lightOn();
      break;
    case Cmd::LightOff:
      say("light: off");
      lightOff();
      break;
    case Cmd::LightAlways:
      fsm.setLightAlways(true, now);
      lightOn();
      say("web: light ALWAYS");
      break;
    case Cmd::LightMotion:
      fsm.setLightAlways(false, now);
      say("web: light on motion");
      break;
    case Cmd::LightPct:
      lightSetPct(c.arg);
      say("web: brightness " + String(c.arg) + "%");
      break;
    case Cmd::IndicatorOn:
      indicatorBase = IndicatorMode::On;
      say("indicator: ON");
      break;
    case Cmd::IndicatorOff:
      indicatorBase = IndicatorMode::Off;
      say("indicator: off");
      break;
    case Cmd::Tare:
      if (!busy()) scaleTare();
      break;
    case Cmd::RawDump:
      scaleRawDump();
      break;
    case Cmd::PirSelfTest:
      pirSelfTest(now);
      break;
    case Cmd::SafeStop:  // OTA is about to write flash: everything to a safe state, then hold
      servosSafe();
      lightCut();
      halted = true;
      break;
    case Cmd::Resume:
      halted = false;
      break;
  }
}

static void applyOutputs(const feeder::Outputs& o) {
  for (int i = 0; i < o.log.count; i++)
    say(o.log.line[i]);
  if (o.rotorUs >= 0) rotorSet(o.rotorUs);
  switch (o.door) {
    case feeder::Door::Open:
      doorMoveTo(ANG_OPEN);
      break;
    case feeder::Door::Close:
      doorMoveTo(ANG_CLOSED);
      break;
    case feeder::Door::CloseAndRelease:
      doorMoveTo(ANG_CLOSED, true);
      break;
    case feeder::Door::None:
      break;
  }
  if (o.light == feeder::Light::On) lightOn();
  if (o.light == feeder::Light::Off) lightOff();
  // the FSM says on/off; blinking comes from the fault word (any critical bit)
  if (o.indicator == feeder::Indicator::On) indicatorBase = IndicatorMode::On;
  if (o.indicator == feeder::Indicator::Off) indicatorBase = IndicatorMode::Off;
}

static uint16_t faultWord(uint32_t now) {
  uint16_t f = 0;
  ScaleHealth sh = scaleHealth();
  if (sh == ScaleHealth::Missing) f = f | Fault::ScaleMissing;
  if (sh == ScaleHealth::Lost) f = f | Fault::ScaleLost;
  if (rotorTripped()) f = f | Fault::RotorTripped;
  if (!aiStatus().online) f = f | Fault::GroveOffline;
  if (fsm.alarm() == feeder::Alarm::NoFood) f = f | Fault::NoFood;
  if (fsm.alarm() == feeder::Alarm::ScaleFault) f = f | Fault::DoseScaleFault;
  if (halted) f = f | Fault::Halted;
  if (bootCrashed()) f = f | Fault::BootCrashed;
  if (lastOverrun && now - lastOverrun < 10000) f = f | Fault::TickOverrun;
  return f;
}

static uint16_t stateWord() {
  uint16_t st = 0;
  if (fsm.dosing()) st = st | State::Dosing;
  if (!doorClosed()) st = st | State::DoorOpen;
  if (doorMoving()) st = st | State::DoorMoving;
  if (scaleHealth() == ScaleHealth::Taring) st = st | State::ScaleTaring;
  if (fsm.catOnTray()) st = st | State::CatOnTray;
  if (fsm.autoMode()) st = st | State::AutoMode;
  if (lightLevel() > 0) st = st | State::LightOn;
  if (fsm.pirMotion()) st = st | State::Motion;
  if (aiBusy()) st = st | State::CameraBusy;
  return st;
}

static void publish() {
  Status s;
  s.scaleOk = scaleOk();
  s.grams = scaleGrams();
  s.pir = fsm.pirMotion();
  s.doorAngle = doorAngle();
  s.doorClosed = doorClosed();
  s.lightLevel = lightLevel();
  s.lightMaxLevel = lightMaxLevel();
  s.autoMode = fsm.autoMode();
  s.dosing = fsm.dosing();
  s.dispensed = fsm.dispensed();
  s.alarm = fsm.alarm();
  s.halted = halted;
  s.faults = faultWord(millis());
  s.states = stateWord();
  s.tickMaxUs = tickMaxUs;
  s.overruns = overruns;
  Locked lock(statusLock);
  status = s;
}

static void tick(uint32_t now) {
  Command c;
  while (xQueueReceive(commands, &c, 0) == pdTRUE)
    apply(c, now);
  if (halted) return;

  float grams = 0;
  bool fresh = scaleTick(now, grams);
  pirTick(now);

  feeder::Inputs in;
  in.now = now;
  in.buttonPressed = buttonTick(now);
  if (in.buttonPressed) rotorClearTrip();  // pressing the button is an operator action too
  in.pirReady = pirReady(now);
  in.pir = pirRaw();
  in.doorClosed = doorClosed();
  in.doorMoving = doorMoving();
  in.scaleOk = scaleOk();
  in.grams = scaleGrams();
  in.sample.fresh = fresh;
  in.sample.grams = grams;
  in.lightLevel = lightLevel();
  applyOutputs(fsm.step(in));

  if (fsm.dosing()) pulseStopAt = 0;  // the dose took over the rotor
  if (pulseStopAt && due(now, pulseStopAt)) {
    pulseStopAt = 0;
    rotorSet(ROT_STOP);
    say("rotor: STOP");
  }
  servosTick(now);
  lightTick(now);
  IndicatorMode want = (faultWord(now) & CRITICAL_FAULTS) ? IndicatorMode::Blink : indicatorBase;
  if (want != indicatorShown) indicatorSet(indicatorShown = want);
  indicatorTick(now);
}

static void task(void*) {
  watchdogSubscribe();
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_TICK_MS));
    watchdogFeed();
    uint32_t t0 = micros();
    tick(millis());
    uint32_t took = micros() - t0;
    if (took > tickMaxUs) tickMaxUs = took;
    if (took > CONTROL_TICK_MS * 1000) {  // the tick overran its slot: timing can't be trusted
      overruns++;
      lastOverrun = millis() | 1;
    }
    publish();
  }
}

void controlBegin() {
  commands = xQueueCreate(16, sizeof(Command));
  // bench defaults: no auto feeding, light follows motion, indicator on
  fsm.setAuto(false);
  indicatorSet(IndicatorMode::On);
  publish();
  // core 1 with the loop, but at priority 5 it preempts the web and the console
  xTaskCreatePinnedToCore(task, "control", 8 * 1024, nullptr, 5, nullptr, 1);
}

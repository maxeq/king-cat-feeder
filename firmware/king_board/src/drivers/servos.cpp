#include "servos.h"
#include <Arduino.h>
#include <ESP32Servo.h>
#include "../clock.h"
#include "../config.h"
#include "../core/log.h"
#include "../core/store.h"

static Servo rotor, lid;

// rotor
static bool rotorAttached = true;
static int rotorCur = ROT_STOP;
static bool rotorRunning = false;   // a forward or back command is active...
static uint32_t rotorRunSince = 0;  // ...since this time
static Deadline rotorIdle;          // FS90R creeps at 1500, so detach when idle
static bool tripped = false;        // safety stop latched until an operator clears it

// door
static bool lidAttached = false;
static int lidCur = ANG_CLOSED, lidTarget = ANG_CLOSED, lidSaved = ANG_CLOSED;
static bool lidRelease = false, lidMoving = false;
static Deadline lidStep;

void servosBegin() {
  ESP32PWM::allocateTimer(1);
  rotor.attach(PIN_ROTOR, SERVO_MIN_US, SERVO_MAX_US);
  rotor.writeMicroseconds(ROT_STOP);
  rotorIdle.start(millis(), ROTOR_IDLE_DETACH_MS);
  lidCur = lidTarget = lidSaved = storeGet(Key::Door, ANG_CLOSED);
  if (lidCur != ANG_CLOSED) sayf("door: left at %d deg before the reboot", lidCur);
}

bool rotorSet(int us) {
  if (tripped && us != ROT_STOP) return false;
  // already there; a stopped rotor stays detached instead of re-attaching at 1500 (it creeps)
  if (us == rotorCur && (rotorAttached || us == ROT_STOP)) return true;
  if (!rotorAttached) {
    rotor.attach(PIN_ROTOR, SERVO_MIN_US, SERVO_MAX_US);
    rotorAttached = true;
  }
  rotor.writeMicroseconds(us);
  rotorCur = us;
  uint32_t now = millis();
  rotorRunning = us != ROT_STOP;
  rotorRunSince = now;
  if (rotorRunning)
    rotorIdle.stop();
  else
    rotorIdle.start(now, ROTOR_IDLE_DETACH_MS);
  return true;
}

void rotorClearTrip() { tripped = false; }
bool rotorTripped() { return tripped; }

int rotorUs() { return rotorCur; }

static void saveDoor() {  // queued to the nvs worker: never stalls the tick
  storePut(Key::Door, lidCur);
  lidSaved = lidCur;
}

void doorMoveTo(int angle, bool releaseWhenThere) {
  lidTarget = constrain(angle, 0, 180);
  lidRelease = releaseWhenThere;
  lidMoving = true;
  if (!lidAttached) {  // attach at the remembered angle, then give the servo a moment
    lid.attach(PIN_LID, SERVO_MIN_US, SERVO_MAX_US);
    lid.write(lidCur);
    lidAttached = true;
    lidStep.start(millis(), DOOR_ATTACH_MS);
  }
}

static void lidDetach() {
  if (lidAttached) lid.detach();
  lidAttached = false;
}

void doorRelease() {
  lidMoving = false;
  lidStep.stop();
  saveDoor();  // stopped mid-move: remember where, so the next attach doesn't jump
  lidDetach();
  pinMode(PIN_LID, OUTPUT);
  digitalWrite(PIN_LID, LOW);  // a floating line picks up noise
}

int doorAngle() { return lidCur; }
bool doorClosed() { return lidCur > (ANG_CLOSED + ANG_OPEN) / 2; }
bool doorMoving() { return lidMoving; }

void servosSafe() {
  rotor.writeMicroseconds(ROT_STOP);
  rotor.detach();
  rotorAttached = false;
  rotorCur = ROT_STOP;
  rotorRunning = false;
  rotorIdle.stop();
  pinMode(PIN_ROTOR, OUTPUT);
  digitalWrite(PIN_ROTOR, LOW);
  doorRelease();
}

void servosTick(uint32_t now) {
  // rotorSet() stamps millis() mid-tick, up to 1 ms past this tick's `now`: elapsed() reads that as 0
  if (rotorRunning && elapsed(now, rotorRunSince) > ROTOR_MAX_RUN_MS) {
    rotorSet(ROT_STOP);
    tripped = true;
    errorf("rotor: SAFETY STOP, ran %lu s on one command (s or the button re-arms)",
           (unsigned long)(ROTOR_MAX_RUN_MS / 1000));
  }
  if (rotorIdle.expired(now)) {
    rotor.detach();
    rotorAttached = false;
    pinMode(PIN_ROTOR, OUTPUT);
    digitalWrite(PIN_ROTOR, LOW);
  }

  if (!lidMoving || !lidAttached) return;
  if (lidStep.running(now)) return;
  if (lidCur != lidTarget) {  // one degree per STEP_MS
    lidCur += (lidTarget > lidCur) ? 1 : -1;
    lid.write(lidCur);
    // a power cut mid-move leaves the servo where it was: keep NVS within a few degrees of it
    if (abs(lidCur - lidSaved) >= DOOR_SAVE_EVERY_DEG) saveDoor();
    lidStep.start(now, STEP_MS);
    return;
  }
  lidMoving = false;
  saveDoor();
  const char* where = lidCur == ANG_OPEN ? "OPEN" : lidCur == ANG_CLOSED ? "CLOSED" : nullptr;
  if (lidRelease) doorRelease();
  if (where)
    sayf("door: %s%s", where, lidRelease ? " (detach+LOW)" : "");
  else
    sayf("door: angle %d%s", lidCur, lidRelease ? " (detach+LOW)" : "");
}

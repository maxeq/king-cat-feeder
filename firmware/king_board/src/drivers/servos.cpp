#include "servos.h"
#include "../config.h"
#include "../core/log.h"
#include "../core/store.h"
#include "../clock.h"
#include <Arduino.h>
#include <ESP32Servo.h>

static Servo rotor, lid;

// rotor
static bool rotorAttached = true;
static int rotorCur = ROT_STOP;
static uint32_t rotorRunSince = 0;  // 0 = stopped
static uint32_t rotorIdleAt = 0;    // FS90R creeps at 1500, so detach when idle
static bool tripped = false;        // safety stop latched until an operator clears it

// door
static bool lidAttached = false;
static int lidCur = ANG_CLOSED, lidTarget = ANG_CLOSED;
static bool lidRelease = false, lidMoving = false;
static uint32_t lidNextStep = 0;

void servosBegin() {
  ESP32PWM::allocateTimer(1);
  rotor.attach(PIN_ROTOR, 500, 2500);
  rotor.writeMicroseconds(ROT_STOP);
  rotorIdleAt = millis() + 1500;
  lidCur = lidTarget = storeGet("door", ANG_CLOSED);
  if (lidCur != ANG_CLOSED) say("door: left at " + String(lidCur) + "° before the reboot");
}

bool rotorSet(int us) {
  if (tripped && us != ROT_STOP) return false;
  // already there; a stopped rotor stays detached instead of re-attaching at 1500 (it creeps)
  if (us == rotorCur && (rotorAttached || us == ROT_STOP)) return true;
  if (!rotorAttached) {
    rotor.attach(PIN_ROTOR, 500, 2500);
    rotorAttached = true;
  }
  rotor.writeMicroseconds(us);
  rotorCur = us;
  uint32_t now = millis();
  rotorIdleAt = (us == ROT_STOP) ? now + 1500 : 0;
  rotorRunSince = (us == ROT_STOP) ? 0 : now;
  return true;
}

void rotorClearTrip() { tripped = false; }
bool rotorTripped() { return tripped; }

int rotorUs() { return rotorCur; }

void doorMoveTo(int angle, bool releaseWhenThere) {
  lidTarget = constrain(angle, 0, 180);
  lidRelease = releaseWhenThere;
  lidMoving = true;
  if (!lidAttached) {  // attach at the remembered angle, then give the servo a moment
    lid.attach(PIN_LID, 500, 2500);
    lid.write(lidCur);
    lidAttached = true;
    lidNextStep = (millis() + 200) | 1;
  }
}

static void lidDetach() {
  if (lidAttached) lid.detach();
  lidAttached = false;
}

void doorRelease() {
  lidMoving = false;
  storePut("door", lidCur);  // stopped mid-move: remember where, so the next attach doesn't jump
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
  rotorRunSince = rotorIdleAt = 0;
  pinMode(PIN_ROTOR, OUTPUT);
  digitalWrite(PIN_ROTOR, LOW);
  doorRelease();
}

void servosTick(uint32_t now) {
  if (rotorRunSince && now - rotorRunSince > ROTOR_MAX_RUN_MS) {
    rotorSet(ROT_STOP);
    tripped = true;
    say("rotor: SAFETY STOP, ran " + String(ROTOR_MAX_RUN_MS / 1000) +
        " s on one command (s or the button re-arms)");
  }
  if (rotorIdleAt && due(now, rotorIdleAt)) {
    rotor.detach();
    rotorAttached = false;
    rotorIdleAt = 0;
    pinMode(PIN_ROTOR, OUTPUT);
    digitalWrite(PIN_ROTOR, LOW);
  }

  if (!lidMoving || !lidAttached) return;
  if (lidNextStep) {
    if (!due(now, lidNextStep)) return;
    lidNextStep = 0;
  }
  if (lidCur != lidTarget) {  // one degree per STEP_MS
    lidCur += (lidTarget > lidCur) ? 1 : -1;
    lid.write(lidCur);
    lidNextStep = (now + STEP_MS) | 1;
    return;
  }
  lidMoving = false;
  storePut("door", lidCur);
  const char* where = lidCur == ANG_OPEN ? "OPEN" : lidCur == ANG_CLOSED ? "CLOSED" : nullptr;
  String msg = where ? String("door: ") + where : "door: angle " + String(lidCur);
  if (lidRelease) {
    doorRelease();
    msg += " (detach+LOW)";
  }
  say(msg);
}

#include "scale.h"
#include "../config.h"
#include "../core/log.h"
#include "../core/store.h"
#include "../clock.h"
#include <Arduino.h>
#include "HX711.h"

static HX711 hx;
static const float CAL_SCALE = 363.7;
static const uint32_t LOST_MS = 3000;  // no reading for this long = cable gone

enum class State { Absent, Probing, Taring, Ready };
static State state = State::Absent;
static bool began = false, skipNext = false, recovering = false;
static uint32_t retryAt = 0, probeUntil = 0, lastSample = 0;
static float grams = 0, tareSum = 0;
static int tareCount = 0, dumpLeft = 0;
static String dumpLine;

bool scaleOk() { return state == State::Ready; }

static bool everReady = false;
ScaleHealth scaleHealth() {
  if (state == State::Ready) return ScaleHealth::Ready;
  if (state == State::Taring) return ScaleHealth::Taring;
  return everReady ? ScaleHealth::Lost : ScaleHealth::Missing;
}
float scaleGrams() { return grams; }

// HX711::begin() with a reset reads the chip and hangs forever if none is there,
// and the library's pins are unset until begin() runs. So probe DT first, and
// begin without the reset: the chip powers up in the right mode (channel A, gain 128).
static void probePins() {
  pinMode(PIN_SC_SCK, OUTPUT);
  digitalWrite(PIN_SC_SCK, LOW);
  pinMode(PIN_SC_DT, INPUT_PULLUP);
}

static void startHx() {
  if (!began) hx.begin(PIN_SC_DT, PIN_SC_SCK, false, false);
  began = true;
  hx.set_scale(CAL_SCALE);
}

// a load on the tray at start-up (kibble, the cat) must not become the new zero:
// keep the saved tare if the reading sits well above it. Lighter is fine: re-tare.
static void applyTare(int32_t raw, bool atBoot) {
  float above = storeHas("tare") ? (raw - storeGet("tare", 0)) / CAL_SCALE : 0;
  if (above > TRAY_LOADED_G) {
    hx.set_offset(storeGet("tare", 0));
    say("scale: tray not empty (" + String(above, 1) + " g), keeping the saved tare");
    return;
  }
  hx.set_offset(raw);
  storePut("tare", raw);
  if (atBoot) Serial.println("scale OK (auto-tare)");
}

void scaleBegin() {
  probePins();
  delay(5);
  bool present = false;
  for (uint32_t t0 = millis(); millis() - t0 < 500 && !present;)
    present = digitalRead(PIN_SC_DT) == LOW;
  retryAt = 10000;
  if (!present) {
    Serial.println("scale NOT CONNECTED — skipped (boot continues)");
    return;
  }
  startHx();
  if (!hx.wait_ready_timeout(1500)) {
    Serial.println("scale NOT RESPONDING (DT/SCK swapped? 3.3V supply?)");
    return;
  }
  hx.read();  // first sample after idle is junk
  float sum = 0;
  for (int i = 0; i < 5; i++) {
    if (!hx.wait_ready_timeout(300)) {
      Serial.println("scale NOT RESPONDING during tare");
      return;
    }
    sum += hx.read();
  }
  applyTare(sum / 5, true);
  state = State::Ready;
  everReady = true;
  lastSample = millis();
}

void scaleTare() {
  if (state != State::Ready) {
    say("tare: scale not ready");
    return;
  }
  state = State::Taring;
  tareSum = 0;
  tareCount = 0;
  recovering = false;
}

void scaleRawDump() {
  if (state != State::Ready) {
    say("scale not responding");
    return;
  }
  dumpLeft = 6;
  dumpLine = "raw:";
}

bool scaleTick(uint32_t now, float& out) {
  switch (state) {
    case State::Absent:
      if (due(now, retryAt)) {
        probePins();
        state = State::Probing;
        probeUntil = now + 200;
      }
      return false;
    case State::Probing:
      if (digitalRead(PIN_SC_DT) == LOW) {  // a chip is pulling DT low: plugged in
        startHx();
        state = State::Taring;
        tareSum = 0;
        tareCount = 0;
        skipNext = recovering = true;
        lastSample = now;
      } else if (due(now, probeUntil)) {
        state = State::Absent;
        retryAt = now + 5000;
      }
      return false;
    case State::Taring:
    case State::Ready:
      break;
  }

  if (!hx.is_ready()) {
    if (now - lastSample > LOST_MS) {
      state = State::Absent;
      retryAt = now + 5000;
      say("scale LOST — no data for " + String(LOST_MS / 1000) + " s");
    }
    return false;
  }
  float raw = hx.read();  // ready, so this returns at once
  lastSample = now;
  if (skipNext) {
    skipNext = false;
    return false;
  }
  if (state == State::Taring) {
    tareSum += raw;
    if (++tareCount == 5) {
      int32_t raw5 = tareSum / 5;
      if (recovering) {  // plugged in again: same rule as at boot
        applyTare(raw5, false);
        say("scale RECOVERED");
      } else {  // an explicit tare: the user says the tray is empty
        hx.set_offset(raw5);
        storePut("tare", raw5);
        say("tare: zero set");
      }
      state = State::Ready;
      everReady = true;
      grams = 0;
    }
    return false;
  }
  if (dumpLeft > 0) {
    dumpLine += " " + String((long)raw);
    if (--dumpLeft == 0) say(dumpLine);
  }
  out = grams = (raw - hx.get_offset()) / hx.get_scale();
  return true;
}

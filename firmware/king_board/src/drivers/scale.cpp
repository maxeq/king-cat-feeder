#include "scale.h"
#include <Arduino.h>
#include "../clock.h"
#include "../config.h"
#include "../core/log.h"
#include "../core/store.h"
#include "HX711.h"

namespace {

HX711 hx;

enum class ScaleState { Absent, Probing, Taring, Ready };
ScaleState state = ScaleState::Absent;
bool began = false, skipNext = false, recovering = false, everReady = false;
Deadline retry, probe;
uint32_t lastSample = 0;
float grams = 0, tareSum = 0;
int tareCount = 0, dumpLeft = 0;
char dumpLine[96];

// HX711::begin() with a reset reads the chip and hangs forever if none is there,
// and the library's pins are unset until begin() runs. So probe DT first, and
// begin without the reset: the chip powers up in the right mode (channel A, gain 128).
void probePins() {
  pinMode(PIN_SC_SCK, OUTPUT);
  digitalWrite(PIN_SC_SCK, LOW);
  pinMode(PIN_SC_DT, INPUT_PULLUP);
}

void startHx() {
  if (!began) hx.begin(PIN_SC_DT, PIN_SC_SCK, false, false);
  began = true;
  hx.set_scale(SCALE_CAL);
}

// a load on the tray at start-up (kibble, the cat) must not become the new zero:
// keep the saved tare if the reading sits well above it. Lighter is fine: re-tare.
void applyTare(int32_t raw, bool atBoot) {
  float above = storeHas(Key::Tare) ? (float)(raw - storeGet(Key::Tare, 0)) / SCALE_CAL : 0;
  if (above > TRAY_LOADED_G) {
    hx.set_offset(storeGet(Key::Tare, 0));
    sayf("scale: tray not empty (%.1f g), keeping the saved tare", above);
    return;
  }
  hx.set_offset(raw);
  storePut(Key::Tare, raw);
  if (atBoot) say("scale OK (auto-tare)");
}

// The library's read() waits for DT without a limit, so a cable pulled between is_ready()
// and read() would hang the control tick until the watchdog. This read never waits: it runs
// only while DT is low (a conversion is ready), clocks out 24 bits MSB first, then one more
// clock for channel A at gain 128. Interrupts stay off for the ~50 us it takes, so SCK is
// never high for 60 us, which would power the chip down.
portMUX_TYPE hxMux = portMUX_INITIALIZER_UNLOCKED;

bool readRaw(int32_t& out) {
  if (digitalRead(PIN_SC_DT) != LOW) return false;
  uint32_t v = 0;
  portENTER_CRITICAL(&hxMux);
  for (int i = 0; i < 24; i++) {
    digitalWrite(PIN_SC_SCK, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | (uint32_t)digitalRead(PIN_SC_DT);
    digitalWrite(PIN_SC_SCK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(PIN_SC_SCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_SC_SCK, LOW);
  portEXIT_CRITICAL(&hxMux);
  if (v & 0x800000) v |= 0xFF000000;  // 24-bit two's complement
  out = (int32_t)v;
  return true;
}

void startTare() {
  state = ScaleState::Taring;
  tareSum = 0;
  tareCount = 0;
}

}  // namespace

bool scaleOk() { return state == ScaleState::Ready; }

ScaleHealth scaleHealth() {
  if (state == ScaleState::Ready) return ScaleHealth::Ready;
  if (state == ScaleState::Taring) return ScaleHealth::Taring;
  return everReady ? ScaleHealth::Lost : ScaleHealth::Missing;
}
float scaleGrams() { return grams; }

void scaleBegin() {
  probePins();
  delay(SCALE_PIN_SETTLE_MS);
  bool present = false;
  for (uint32_t t0 = millis(); millis() - t0 < SCALE_BOOT_PROBE_MS && !present;)
    present = digitalRead(PIN_SC_DT) == LOW;
  retry.start(millis(), SCALE_FIRST_RETRY_MS);
  if (!present) {
    warnf("scale NOT CONNECTED: skipped (boot continues)");
    return;
  }
  startHx();
  if (!hx.wait_ready_timeout(SCALE_BOOT_READY_MS)) {
    errorf("scale NOT RESPONDING (DT/SCK swapped? 3.3V supply?)");
    return;
  }
  int32_t raw = 0;
  readRaw(raw);  // first sample after idle is junk
  float sum = 0;
  for (int i = 0; i < SCALE_TARE_SAMPLES; i++) {
    if (!hx.wait_ready_timeout(SCALE_SAMPLE_WAIT_MS) || !readRaw(raw)) {
      errorf("scale NOT RESPONDING during tare");
      return;
    }
    sum += (float)raw;
  }
  applyTare((int32_t)(sum / SCALE_TARE_SAMPLES), true);
  state = ScaleState::Ready;
  everReady = true;
  lastSample = millis();
}

void scaleTare() {
  if (state != ScaleState::Ready) {
    say("tare: scale not ready");
    return;
  }
  startTare();
  recovering = false;
}

void scaleRawDump() {
  if (state != ScaleState::Ready) {
    say("scale not responding");
    return;
  }
  dumpLeft = SCALE_DUMP_SAMPLES;
  strlcpy(dumpLine, "raw:", sizeof(dumpLine));
}

bool scaleTick(uint32_t now, float& out) {
  switch (state) {
    case ScaleState::Absent:
      if (retry.expired(now)) {
        probePins();
        state = ScaleState::Probing;
        probe.start(now, SCALE_PROBE_MS);
      }
      return false;
    case ScaleState::Probing:
      if (digitalRead(PIN_SC_DT) == LOW) {  // a chip is pulling DT low: plugged in
        startHx();
        startTare();
        probe.stop();
        skipNext = recovering = true;
        lastSample = now;
      } else if (probe.expired(now)) {
        state = ScaleState::Absent;
        retry.start(now, SCALE_RETRY_MS);
      }
      return false;
    case ScaleState::Taring:
    case ScaleState::Ready:
      break;
  }

  int32_t raw = 0;
  if (!readRaw(raw)) {  // no conversion ready (or no chip): never waits
    if (elapsed(now, lastSample) > SCALE_LOST_MS) {
      state = ScaleState::Absent;
      retry.start(now, SCALE_RETRY_MS);
      warnf("scale LOST: no data for %lu s", (unsigned long)(SCALE_LOST_MS / 1000));
    }
    return false;
  }
  lastSample = now;
  if (skipNext) {
    skipNext = false;
    return false;
  }
  if (state == ScaleState::Taring) {
    tareSum += (float)raw;
    if (++tareCount == SCALE_TARE_SAMPLES) {
      int32_t avg = (int32_t)(tareSum / SCALE_TARE_SAMPLES);
      if (recovering) {  // plugged in again: same rule as at boot
        applyTare(avg, false);
        say("scale RECOVERED");
      } else {  // an explicit tare: the user says the tray is empty
        hx.set_offset(avg);
        storePut(Key::Tare, avg);
        say("tare: zero set");
      }
      state = ScaleState::Ready;
      everReady = true;
      grams = 0;
    }
    return false;
  }
  if (dumpLeft > 0) {
    size_t n = strlen(dumpLine);
    snprintf(dumpLine + n, sizeof(dumpLine) - n, " %ld", (long)raw);
    if (--dumpLeft == 0) say(dumpLine);
  }
  out = grams = (float)(raw - hx.get_offset()) / hx.get_scale();
  return true;
}

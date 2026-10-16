#include "light.h"
#include <Arduino.h>
#include "../clock.h"
#include "../config.h"
#include "../core/store.h"

static int level = 0, maxLevel = LIGHT_DEFAULT_LEVEL;
static int rampFrom = 0, rampTo = 0;
static uint32_t rampStart = 0;
static bool ramping = false;

void lightBegin() {
  maxLevel = storeGet(Key::Light, maxLevel);
  ledcAttachChannel(PIN_LED, LED_FREQ_HZ, LED_BITS, LED_CH);
  ledcWriteChannel(LED_CH, 0);
}

static void rampTo_(int to) {
  if (to == rampTo && (ramping || level == to)) return;
  rampFrom = level;
  rampTo = to;
  rampStart = millis();
  ramping = true;
}

void lightOn() { rampTo_(maxLevel); }
void lightOff() { rampTo_(0); }

void lightCut() {
  ramping = false;
  level = rampTo = 0;
  ledcWriteChannel(LED_CH, 0);
}

void lightSetPct(int pct) {
  maxLevel = constrain(pct, 0, 100) * 255 / 100;
  storePut(Key::Light, maxLevel);  // queued to the nvs worker
  if (level > 0 || rampTo > 0) rampTo_(maxLevel);
}

int lightLevel() { return level; }
int lightMaxLevel() { return maxLevel; }

void lightTick(uint32_t now) {
  if (!ramping) return;
  uint32_t t = elapsed(now, rampStart);  // a ramp started mid-tick is stamped after `now`
  if (t >= LIGHT_RAMP_MS) {
    level = rampTo;
    ramping = false;
  } else {
    level = rampFrom + (rampTo - rampFrom) * (int)t / (int)LIGHT_RAMP_MS;
  }
  ledcWriteChannel(LED_CH, (uint32_t)level);
}

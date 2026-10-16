#include "light.h"
#include "../config.h"
#include "../core/store.h"
#include <Arduino.h>

static int level = 0, maxLevel = 51;
static int rampFrom = 0, rampTo = 0;
static uint32_t rampStart = 0;
static bool ramping = false;

void lightBegin() {
  maxLevel = storeGet("light", maxLevel);
  ledcAttachChannel(PIN_LED, 5000, 8, LED_CH);
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
  storePut("light", maxLevel);
  if (level > 0 || rampTo > 0) rampTo_(maxLevel);
}

int lightLevel() { return level; }
int lightMaxLevel() { return maxLevel; }

void lightTick(uint32_t now) {
  if (!ramping) return;
  uint32_t t = now - rampStart;
  if (t >= LIGHT_RAMP_MS) {
    level = rampTo;
    ramping = false;
  } else {
    level = rampFrom + (rampTo - rampFrom) * (int)t / (int)LIGHT_RAMP_MS;
  }
  ledcWriteChannel(LED_CH, level);
}

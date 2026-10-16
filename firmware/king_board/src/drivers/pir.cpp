#include "pir.h"
#include "../config.h"
#include "../core/log.h"
#include "../clock.h"
#include <Arduino.h>

static bool level = false;
static int testStep = 0;  // 0 = no test running
static uint32_t testAt = 0;
static int up = 0;

void pirBegin() { pinMode(PIN_PIR, INPUT); }
bool pirReady(uint32_t now) {
  static bool warm = false;  // latched, so a millis() wrap doesn't restart the warm-up
  return warm = warm || now > PIR_WARMUP_MS;
}
bool pirRaw() { return level; }

void pirSelfTest(uint32_t now) {
  pinMode(PIN_PIR, INPUT_PULLUP);
  testStep = 1;
  testAt = now + 50;
}

void pirTick(uint32_t now) {
  if (!testStep) {
    level = digitalRead(PIN_PIR);
    return;
  }
  if (!due(now, testAt)) return;
  if (testStep == 1) {
    up = digitalRead(PIN_PIR);
    pinMode(PIN_PIR, INPUT_PULLDOWN);
    testStep = 2;
    testAt = now + 50;
    return;
  }
  int dn = digitalRead(PIN_PIR);
  pinMode(PIN_PIR, INPUT);
  testStep = 0;
  say("PIR self-test: pullup=" + String(up) + " pulldown=" + String(dn) +
      (up == 1 && dn == 0   ? " — PIN OK (cable/module at fault)"
       : up == 1 && dn == 1 ? " — something HOLDS the pin high"
       : up == 0            ? " — something PULLS the pin low (cable?)"
                            : " — odd"));
}

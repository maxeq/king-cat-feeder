#include "indicator.h"
#include "../config.h"
#include <Arduino.h>

static IndicatorMode mode = IndicatorMode::Off;

void indicatorBegin() {
  pinMode(PIN_IND, OUTPUT);
  digitalWrite(PIN_IND, LOW);
}

void indicatorSet(IndicatorMode m) {
  mode = m;
  if (m != IndicatorMode::Blink) digitalWrite(PIN_IND, m == IndicatorMode::On ? HIGH : LOW);
}

void indicatorTick(uint32_t now) {
  if (mode == IndicatorMode::Blink) digitalWrite(PIN_IND, (now / 400) % 2);
}

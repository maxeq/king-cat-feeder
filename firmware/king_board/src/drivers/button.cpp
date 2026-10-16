#include "button.h"
#include "../config.h"
#include "../clock.h"
#include <Arduino.h>

static bool last = true;  // HIGH = released
static uint32_t quietUntil = 0;

void buttonBegin() { pinMode(PIN_BTN, INPUT_PULLUP); }

bool buttonTick(uint32_t now) {
  bool level = digitalRead(PIN_BTN);
  if (quietUntil && due(now, quietUntil)) quietUntil = 0;
  if (level == last || quietUntil) return false;
  last = level;
  quietUntil = (now + 300) | 1;  // never 0: 0 means "no debounce running"
  return level == LOW;
}

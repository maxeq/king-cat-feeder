#include "button.h"
#include <Arduino.h>
#include "../clock.h"
#include "../config.h"

static bool last = true;  // HIGH = released
static Deadline quiet;    // debounce: edges are ignored until it expires

void buttonBegin() { pinMode(PIN_BTN, INPUT_PULLUP); }

bool buttonTick(uint32_t now) {
  bool level = digitalRead(PIN_BTN);
  bool settling = quiet.running(now);  // poll every tick, not only on an edge: see Deadline
  if (level == last || settling) return false;
  last = level;
  quiet.start(now, BUTTON_DEBOUNCE_MS);
  return level == LOW;
}

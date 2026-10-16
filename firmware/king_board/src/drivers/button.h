#pragma once
#include <stdint.h>

// Push button to GND, internal pull-up, 300 ms debounce.
void buttonBegin();
bool buttonTick(uint32_t now);  // true once per press

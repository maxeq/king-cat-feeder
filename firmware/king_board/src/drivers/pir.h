#pragma once
#include <stdint.h>

// HC-SR501 PIR. Wakes the camera at night; it doesn't decide who's there.
void pirBegin();
void pirTick(uint32_t now);
bool pirReady(uint32_t now);     // false during the sensor's warm-up after boot
bool pirRaw();                   // last real reading (held while a self-test runs)
void pirSelfTest(uint32_t now);  // pin or wire? reports via say() ~100 ms later

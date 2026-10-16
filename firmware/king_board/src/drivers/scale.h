#pragma once
#include <stdint.h>
#include "../types.h"

// HX711 load cell under the tray. Non-blocking after scaleBegin(): scaleTick()
// picks up a reading whenever the HX711 has one (10 per second).
void scaleBegin();                           // setup only: probe and tare, may block ~2 s
bool scaleTick(uint32_t now, float& grams);  // true when a fresh reading arrived
bool scaleOk();
ScaleHealth scaleHealth();
float scaleGrams();   // last reading
void scaleTare();     // averages the next 5 readings, then saves the tare
void scaleRawDump();  // logs the next 6 raw readings

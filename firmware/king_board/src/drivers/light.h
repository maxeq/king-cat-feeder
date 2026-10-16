#pragma once
#include <stdint.h>

// LED light on a PWM channel. Changes ramp over LIGHT_RAMP_MS so the light
// doesn't startle the cat. Non-blocking: lightTick() advances the ramp.
void lightBegin();  // restores the "on" level from NVS
void lightTick(uint32_t now);
void lightOn();
void lightOff();
void lightCut();            // off at once, no ramp (before OTA)
void lightSetPct(int pct);  // the "on" level, saved in NVS
int lightLevel();           // current, 0..255
int lightMaxLevel();        // the "on" level, 0..255

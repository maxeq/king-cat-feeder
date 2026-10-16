#pragma once
#include <stdint.h>

// Status LED: on, off or blinking (400 ms phases).
enum class IndicatorMode { Off, On, Blink };
void indicatorBegin();
void indicatorSet(IndicatorMode m);
void indicatorTick(uint32_t now);

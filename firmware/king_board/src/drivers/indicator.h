#pragma once
#include <stdint.h>
#include "../types.h"

// Status LED: on, off or blinking (IndicatorMode in types.h).
void indicatorBegin();
void indicatorSet(IndicatorMode m);
void indicatorTick(uint32_t now);

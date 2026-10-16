#pragma once
#include <Arduino.h>
#include "../app/control.h"

// The board's fault word as the network sees it: the control task's bits plus the
// ones only the network layer knows (Wi-Fi). Shared by /status, /health and the console.
uint16_t boardFaults(const Status& s);
String bitNames(uint16_t word, const char* (*name)(int));  // "ScaleLost,RotorTripped" or "-"
String hex4(uint16_t word);                                // "0x0005"

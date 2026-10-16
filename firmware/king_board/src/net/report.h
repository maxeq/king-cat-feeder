#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../app/control.h"

// The board's fault word as the network sees it: the control task's bits plus the
// ones only the network layer knows (Wi-Fi). Shared by /status, /health and the console.
uint16_t boardFaults(const Status& s);
// "ScaleLost,RotorTripped" or "-", into out
void bitNames(uint16_t bits, const char* (*name)(int), char* out, size_t cap);

// Writes to a socket without blocking, against one deadline (millis()) for the whole
// message: a peer that reads slowly, or not at all, gets dropped when it passes, so the
// loop task never waits on a peer longer than the caller allows. Feeds the watchdog.
bool sockSend(int fd, const char* data, size_t len, uint32_t deadline);

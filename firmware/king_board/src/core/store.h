#pragma once
#include <stdint.h>

// Settings that survive a reboot (NVS): light level, tare, door angle.
// Call from the control task only (and setup), so writes never race.
void storeBegin();
int32_t storeGet(const char* key, int32_t fallback);
bool storeHas(const char* key);
void storePut(const char* key, int32_t value);  // skips the flash write if unchanged

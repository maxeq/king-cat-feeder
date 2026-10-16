#pragma once
#include <stdint.h>
#include "../types.h"  // Key

// Settings that survive a reboot (NVS). Reads come from a RAM copy loaded at boot, and
// writes go to the "nvs" worker: a flash write, or a page erase, takes milliseconds, so the
// control tick never waits for one. After storeRun() the worker is the only code that
// touches NVS (one owner); before it, setup() writes directly, since no other task runs yet.

void storeBegin();  // setup(): load every key
void storeRun();    // setup(): start the nvs worker; from here on writes are queued
int32_t storeGet(Key k, int32_t fallback);
bool storeHas(Key k);
void storePut(Key k, int32_t value);  // any task, never blocks; skips unchanged values
uint32_t storeStackFree();            // the worker's smallest free stack, bytes

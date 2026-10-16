#pragma once
#include <stdint.h>
#include "../types.h"  // AiResult, AiFrame, AiStatus

// Grove Vision AI V2 (camera + NPU) over I2C, SSCMA protocol.
// Bus work runs as jobs on the "i2c" worker (core 0); these calls return at once.
// Frames live in three fixed PSRAM buffers allocated at boot. The worker fills one that
// nobody uses, then publishes it; a reader pins the published one while it streams it out.
// So a frame is never copied or allocated per request, and a reader never sees a half frame.

void groveBegin();         // bus + model check, frame buffers, then starts the i2c worker
void aiRequestFrame();     // snapshot + inference; skipped if a request is already pending
void aiRequestInvoke();    // inference only (console 'v')
void aiRequestSnapshot();  // snapshot only (console 'S')
void aiRequestScan();      // I2C scan + raw AT+ID? (console 'g')
bool aiBusy();             // a request is queued or running
AiFrame aiPin();           // the loop task: the latest frame, held until aiRelease()
void aiRelease();
uint32_t aiStackFree();  // the i2c worker's smallest free stack, bytes

AiStatus aiStatus();  // lock-free: safe from the control tick

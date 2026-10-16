#pragma once
#include <Arduino.h>

// Grove Vision AI V2 (camera + NPU) over I2C, SSCMA protocol.
// Bus work runs as jobs on the "i2c" worker (core 0); these calls return at once.
struct AiResult {
  bool online = false;
  int count = 0, best = 0;  // objects found, best confidence %
  String verdict = "no data";
  String image;          // base64 jpeg of the latest frame
  unsigned long ms = 0;  // how long the last request took
  unsigned long at = 0;  // millis() when it finished, 0 = never
};

void groveBegin();         // bus + model check, then starts the i2c worker
void aiRequestFrame();     // snapshot + inference; skipped if a request is already pending
void aiRequestInvoke();    // inference only (console 'v')
void aiRequestSnapshot();  // snapshot only (console 'S')
void aiRequestScan();      // I2C scan + raw AT+ID? (console 'g')
bool aiBusy();             // a request is queued or running
AiResult aiLatest();       // copy of the latest result (includes the image), safe from any task

struct AiStatus {
  unsigned long at;  // when the latest frame was taken, 0 = never
  bool online;
};
AiStatus aiStatus();  // cheap: no image copy

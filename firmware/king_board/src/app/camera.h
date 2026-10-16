#pragma once
#include <stdint.h>
#include "../types.h"

// The camera as the network layer sees it: requests that queue a job on the i2c worker,
// and the latest result. Nothing here touches the bus, and every call returns at once,
// like controlSend() and controlStatus(). The network layer includes this, never the driver.
void cameraRequestFrame();     // snapshot + inference; skipped if one is already pending
void cameraRequestInvoke();    // inference only (console 'v')
void cameraRequestSnapshot();  // snapshot only (console 'S')
void cameraRequestScan();      // I2C scan (console 'g')
bool cameraBusy();
AiStatus cameraStatus();  // when the latest frame was taken, and whether the camera answers
AiFrame cameraPin();      // the latest frame, held until cameraRelease(): stream it, then release
void cameraRelease();
uint32_t cameraStackFree();  // the i2c worker's smallest free stack, bytes

#include "camera.h"
#include "../drivers/grove.h"

void cameraRequestFrame() { aiRequestFrame(); }
void cameraRequestInvoke() { aiRequestInvoke(); }
void cameraRequestSnapshot() { aiRequestSnapshot(); }
void cameraRequestScan() { aiRequestScan(); }
bool cameraBusy() { return aiBusy(); }
AiStatus cameraStatus() { return aiStatus(); }
AiFrame cameraPin() { return aiPin(); }
void cameraRelease() { aiRelease(); }
uint32_t cameraStackFree() { return aiStackFree(); }

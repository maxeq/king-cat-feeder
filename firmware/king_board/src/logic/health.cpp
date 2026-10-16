#include "health.h"

const char* faultName(int i) {
  static const char* const names[16] = {"ScaleMissing", "ScaleLost",      "RotorTripped", "GroveOffline",
                                        "NoFood",       "DoseScaleFault", "WifiDown",     "Halted",
                                        "BootCrashed",  "TickOverrun",    "DoseTimeout"};
  return i >= 0 && i < 16 ? names[i] : nullptr;
}

const char* stateName(int i) {
  static const char* const names[16] = {"Dosing",   "DoorOpen", "DoorMoving", "ScaleTaring", "CatOnTray",
                                        "AutoMode", "LightOn",  "Motion",     "CameraBusy"};
  return i >= 0 && i < 16 ? names[i] : nullptr;
}

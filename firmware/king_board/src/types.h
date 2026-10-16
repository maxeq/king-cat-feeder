#pragma once
#include <stddef.h>
#include <stdint.h>

// Plain types more than one layer needs: the drivers and core produce them, the ports in
// app/ and the network layer consume them. No Arduino, no behaviour, so any layer and the
// host tests may include this file, like config.h and clock.h.

// settings that survive a reboot (core/store.h)
enum class Key : uint8_t {
  Door,         // door angle, so the next attach doesn't jump
  Tare,         // HX711 offset
  Light,        // the "on" level, 0..255
  BootFail,     // crashed boots in a row (rollback)
  LightAlways,  // light always on (1) or on motion (0)
  DoseRunning,  // 1 while a dose runs: a reboot in the middle is reported on the next boot
  Count
};

// status LED (drivers/indicator.h): on, off or blinking
enum class IndicatorMode { Off, On, Blink };

// HX711 (drivers/scale.h); Missing = never found since boot
enum class ScaleHealth { Ready, Taring, Lost, Missing };

// the Grove camera (drivers/grove.h, app/camera.h)
struct AiResult {
  bool online = false;
  int count = 0, best = 0;  // objects found, best confidence %
  char verdict[160] = "no data";
  uint32_t ms = 0;  // how long the last request took
  uint32_t at = 0;  // millis() when it finished, 0 = never
};

struct AiFrame {                // a pinned frame: valid until it is released
  const char* image = nullptr;  // base64 jpeg, nullptr = none yet
  size_t len = 0;
  AiResult result;
};

struct AiStatus {
  uint32_t at;  // when the latest frame was taken, 0 = never
  bool online;
};

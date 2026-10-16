#pragma once
#include <Arduino.h>
#include <atomic>
#include "../config.h"

// A FreeRTOS mutex in static memory: safe to create as a global, no heap.
// (std::mutex works too, but pulls in ~50 KB of pthread support.)
// Every wait has a limit: the locks here guard copies of a few bytes, so a wait of
// LOCK_TIMEOUT_MS means a bug. It is counted, shown in /health, and the caller skips its copy.
inline std::atomic<uint32_t> lockTimeouts{0};

class Mutex {
 public:
  Mutex() { handle_ = xSemaphoreCreateMutexStatic(&storage_); }
  bool lock(uint32_t ms) {
    if (xSemaphoreTake(handle_, pdMS_TO_TICKS(ms)) == pdTRUE) return true;
    lockTimeouts++;
    return false;
  }
  void unlock() { xSemaphoreGive(handle_); }
  Mutex(const Mutex&) = delete;  // the handle points into this object's storage
  Mutex& operator=(const Mutex&) = delete;

 private:
  StaticSemaphore_t storage_;
  SemaphoreHandle_t handle_;
};

// holds a Mutex for the rest of the scope, like std::lock_guard; check it before use:
//   Locked lock(m);
//   if (!lock) return;  // timed out
class Locked {
 public:
  explicit Locked(Mutex& m, uint32_t ms = LOCK_TIMEOUT_MS) : m_(m), held_(m.lock(ms)) {}
  ~Locked() {
    if (held_) m_.unlock();
  }
  explicit operator bool() const { return held_; }
  Locked(const Locked&) = delete;
  Locked& operator=(const Locked&) = delete;

 private:
  Mutex& m_;
  bool held_;
};

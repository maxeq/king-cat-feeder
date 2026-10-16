#pragma once
#include <Arduino.h>

// A FreeRTOS mutex in static memory: safe to create as a global, no heap.
// (std::mutex works too, but pulls in ~50 KB of pthread support.)
class Mutex {
 public:
  Mutex() { handle_ = xSemaphoreCreateMutexStatic(&storage_); }
  void lock() { xSemaphoreTake(handle_, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(handle_); }
  Mutex(const Mutex&) = delete;  // the handle points into this object's storage
  Mutex& operator=(const Mutex&) = delete;

 private:
  StaticSemaphore_t storage_;
  SemaphoreHandle_t handle_;
};

// holds a Mutex for the rest of the scope, like std::lock_guard
class Locked {
 public:
  explicit Locked(Mutex& m) : m_(m) { m_.lock(); }
  ~Locked() { m_.unlock(); }
  Locked(const Locked&) = delete;
  Locked& operator=(const Locked&) = delete;

 private:
  Mutex& m_;
};

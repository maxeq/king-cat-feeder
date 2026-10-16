#pragma once
#include <Arduino.h>

// One background task per resource, with its own job queue (the "active object"
// pattern). A device that needs seconds per exchange, like the camera on the I2C
// bus, gets a worker; callers post jobs and carry on. Jobs run one at a time, in
// order, so the worker owns its bus and needs no lock. Separate resources get
// separate workers, so they never wait on each other.
class Worker {
 public:
  using Job = void (*)(int arg);

  Worker(const char* name, int core, uint32_t stackBytes, int queueDepth = 4);
  void begin();
  bool post(Job job, int arg = 0);  // false if the queue is full
  uint32_t stackFree() const;       // smallest free stack since start, bytes (0 before begin())

 private:
  struct Item {
    Job job;
    int arg;
  };
  static void run(void* self);

  const char* name_;
  int core_;
  uint32_t stack_;
  int depth_;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
};

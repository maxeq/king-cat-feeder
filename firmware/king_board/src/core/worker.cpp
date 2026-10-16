#include "worker.h"
#include "../config.h"
#include "watchdog.h"

Worker::Worker(const char* name, int core, uint32_t stackBytes, int queueDepth)
    : name_(name), core_(core), stack_(stackBytes), depth_(queueDepth) {}

void Worker::begin() {
  if (queue_) return;
  queue_ = xQueueCreate(depth_, sizeof(Item));
  // below the control task and Wi-Fi, so a long job never delays them
  xTaskCreatePinnedToCore(run, name_, stack_, this, WORKER_PRIORITY, &task_, core_);
}

bool Worker::post(Job job, int arg) {
  if (!queue_) return false;
  Item item{job, arg};
  return xQueueSend(queue_, &item, 0) == pdTRUE;
}

uint32_t Worker::stackFree() const { return task_ ? uxTaskGetStackHighWaterMark(task_) : 0; }

void Worker::run(void* self) {
  Worker* w = static_cast<Worker*>(self);
  watchdogSubscribe();
  Item item;
  for (;;) {
    watchdogFeed();
    if (xQueueReceive(w->queue_, &item, pdMS_TO_TICKS(WORKER_IDLE_MS)) != pdTRUE)
      continue;  // idle: wake to feed
    item.job(item.arg);
  }
}

#include "worker.h"
#include "watchdog.h"

Worker::Worker(const char* name, int core, uint32_t stackBytes, int queueDepth)
    : name_(name), core_(core), stack_(stackBytes), depth_(queueDepth) {}

void Worker::begin() {
  if (queue_) return;
  queue_ = xQueueCreate(depth_, sizeof(Item));
  // priority 1: below the control task and Wi-Fi, so a long job never delays them
  xTaskCreatePinnedToCore(run, name_, stack_, this, 1, nullptr, core_);
}

bool Worker::post(Job job, int arg) {
  if (!queue_) return false;
  Item item{job, arg};
  pending_++;
  if (xQueueSend(queue_, &item, 0) == pdTRUE) return true;
  pending_--;
  return false;
}

void Worker::run(void* self) {
  Worker* w = static_cast<Worker*>(self);
  watchdogSubscribe();
  Item item;
  for (;;) {
    watchdogFeed();
    if (xQueueReceive(w->queue_, &item, pdMS_TO_TICKS(1000)) != pdTRUE) continue;  // idle: wake to feed
    item.job(item.arg);
    w->pending_--;
  }
}

#include "watchdog.h"
#include <esp_system.h>
#include <esp_task_wdt.h>

void watchdogBegin(unsigned seconds) {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = seconds * 1000;
  cfg.idle_core_mask = 1 << 0;  // keep watching core 0's idle task, as the core does by default
  cfg.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
}

static bool subscribed() { return esp_task_wdt_status(NULL) == ESP_OK; }

void watchdogSubscribe() {
  if (!subscribed()) esp_task_wdt_add(NULL);
}

void watchdogUnsubscribe() {
  if (subscribed()) esp_task_wdt_delete(NULL);
}

void watchdogFeed() {
  if (subscribed()) esp_task_wdt_reset();
}

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "power on";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "crash";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_EXT:
      return "reset pin";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    default:
      return "unknown";
  }
}

#include "rollback.h"
#include "log.h"
#include "store.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

static const int32_t MAX_FAILED_BOOTS = 3;
static const uint32_t HEALTHY_AFTER_MS = 60000;

static bool crashed() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return true;
    default:
      return false;  // power-on, software restart (OTA), brownout: not the firmware's fault
  }
}

bool bootCrashed() { return crashed(); }

void rollbackCheck() {
  if (!crashed()) return;
  int32_t fails = storeGet(Key::BootFail, 0) + 1;
  storePut(Key::BootFail, fails);  // setup: written at once, before a restart below
  if (fails < MAX_FAILED_BOOTS) return;

  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  esp_app_desc_t desc;
  if (!other || esp_ota_get_partition_description(other, &desc) != ESP_OK) {
    errorf("rollback: firmware keeps crashing, but there is no other image to go back to");
    return;
  }
  storePut(Key::BootFail, 0);
  errorf("rollback: %ld crashed boots, switching to %s (%s)", (long)fails, other->label, desc.version);
  if (esp_ota_set_boot_partition(other) == ESP_OK) {
    delay(100);
    esp_restart();
  }
}

void rollbackPoll(uint32_t now) {
  static bool done = false;  // latched, so a millis() wrap doesn't matter
  if (done || now < HEALTHY_AFTER_MS) return;
  done = true;
  if (storeGet(Key::BootFail, 0) != 0) say("boot: healthy, crash counter cleared");
  storePut(Key::BootFail, 0);
}

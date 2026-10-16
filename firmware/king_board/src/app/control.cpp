// The control task: the FreeRTOS side of the Controller. It runs the 10 ms tick, takes
// commands from a queue, publishes a status snapshot under a lock, and measures itself.
// The adapters below are the real hardware behind the ports (ports.h): each method calls one driver.
#include "control.h"
#include <Arduino.h>
#include <atomic>
#include "../config.h"
#include "../core/lock.h"
#include "../core/log.h"
#include "../core/rollback.h"
#include "../core/store.h"
#include "../core/watchdog.h"
#include "../drivers/button.h"
#include "../drivers/grove.h"
#include "../drivers/indicator.h"
#include "../drivers/light.h"
#include "../drivers/pir.h"
#include "../drivers/scale.h"
#include "../drivers/servos.h"
#include "controller.h"
#include "ports.h"

namespace {

// Adapters: each port, implemented by its driver. One method, one driver call.
struct BoardRotor : Rotor {
  bool set(int us) override { return rotorSet(us); }
  void clearTrip() override { rotorClearTrip(); }
  bool tripped() override { return rotorTripped(); }
  int us() override { return rotorUs(); }
};

struct BoardDoor : Door {
  void moveTo(int angle, bool releaseWhenThere) override { doorMoveTo(angle, releaseWhenThere); }
  void release() override { doorRelease(); }
  int angle() override { return doorAngle(); }
  bool closed() override { return doorClosed(); }
  bool moving() override { return doorMoving(); }
};

struct BoardServos : Servos {
  void safe() override { servosSafe(); }
  void tick(uint32_t now) override { servosTick(now); }
};

struct BoardScale : Scale {
  bool tick(uint32_t now, float& grams) override { return scaleTick(now, grams); }
  bool ok() override { return scaleOk(); }
  ScaleHealth health() override { return scaleHealth(); }
  float grams() override { return scaleGrams(); }
  void tare() override { scaleTare(); }
  void rawDump() override { scaleRawDump(); }
};

struct BoardLight : Light {
  void on() override { lightOn(); }
  void off() override { lightOff(); }
  void cut() override { lightCut(); }
  void setPct(int pct) override { lightSetPct(pct); }
  void tick(uint32_t now) override { lightTick(now); }
  int level() override { return lightLevel(); }
  int maxLevel() override { return lightMaxLevel(); }
};

struct BoardIndicator : Indicator {
  void set(IndicatorMode m) override { indicatorSet(m); }
  void tick(uint32_t now) override { indicatorTick(now); }
};

struct BoardInputs : Inputs {
  bool buttonTick(uint32_t now) override { return ::buttonTick(now); }
  void pirTick(uint32_t now) override { ::pirTick(now); }
  bool pirReady(uint32_t now) override { return ::pirReady(now); }
  bool pir() override { return pirRaw(); }
  void pirSelfTest(uint32_t now) override { ::pirSelfTest(now); }
};

struct BoardPeers : Peers {
  bool cameraOnline() override { return aiStatus().online; }
  bool cameraBusy() override { return aiBusy(); }
  bool bootCrashed() override { return ::bootCrashed(); }
};

struct BoardStore : Store {
  int32_t load(Key k, int32_t fallback) override { return storeGet(k, fallback); }
  void save(Key k, int32_t value) override { storePut(k, value); }
};

struct BoardLog : Log {
  void line(const char* text) override { say(text); }
};

struct Command {
  Cmd cmd;
  int arg;
};

// the composition root: the one place where adapters are created and wired to the controller
BoardRotor rotor;
BoardDoor door;
BoardServos servos;
BoardScale scale;
BoardLight light;
BoardIndicator indicator;
BoardInputs inputs;
BoardPeers peers;
BoardStore store;
BoardLog logPort;
Controller ctl({rotor, door, servos, scale, light, indicator, inputs, peers, store, logPort});
QueueHandle_t commands = nullptr;
Mutex statusLock;
Status status;  // guarded by statusLock
std::atomic<bool> haltWanted{false}, resumeWanted{false}, haltedNow{false};

void publish(uint32_t now) {
  Status s = ctl.status(now);
  s.stackFree = uxTaskGetStackHighWaterMark(nullptr);
  Locked lock(statusLock, PUBLISH_LOCK_MS);  // short: this runs inside the 10 ms tick
  if (lock) status = s;  // on a timeout readers keep the previous snapshot for one more tick
}

void task(void*) {
  watchdogSubscribe();
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_TICK_MS));
    watchdogFeed();
    uint32_t t0 = micros();
    uint32_t now = millis();
    if (haltWanted.exchange(false)) ctl.halt();  // before the queue: what was queued is dropped
    if (resumeWanted.exchange(false)) ctl.resume();
    haltedNow = ctl.halted();
    Command c;
    while (xQueueReceive(commands, &c, 0) == pdTRUE)
      ctl.apply(c.cmd, c.arg, now);
    ctl.tick(now);
    if (!ctl.halted()) rollbackPoll(now);  // no NVS write during an OTA
    publish(now);
    ctl.tickTook(micros() - t0, now);  // the whole tick, publishing included
  }
}

}  // namespace

bool controlSend(Cmd cmd, int arg) {
  Command c{cmd, arg};
  return commands && xQueueSend(commands, &c, 0) == pdTRUE;
}

Status controlStatus() {
  Status s;  // valid = false
  Locked lock(statusLock);
  if (lock) s = status;
  return s;
}

bool controlHalt() {
  haltWanted = true;
  for (uint32_t t0 = millis(); !haltedNow && millis() - t0 < HALT_ACK_MS;)
    delay(1);
  return haltedNow;
}

void controlResume() { resumeWanted = true; }

void controlBegin() {
  commands = xQueueCreate(16, sizeof(Command));
  ctl.begin(millis());
  publish(millis());
  // core 1 with the loop, but at CONTROL_PRIORITY it preempts the web and the console
  xTaskCreatePinnedToCore(task, "control", 8 * 1024, nullptr, CONTROL_PRIORITY, nullptr, 1);
}

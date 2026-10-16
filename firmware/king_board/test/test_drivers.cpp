// Host-side tests for the drivers that hold safety logic: servos (the 20 s rotor cap, the
// idle detach, the door steps and saves), the button debounce and the scale (tare, readRaw,
// lost and recovered). The drivers compile unchanged against fakes of the ESP32 core and
// libraries in test/fakes (a link seam). Each test runs in its own process, because the
// drivers keep their state in file-level statics. Build and run:
//   g++ -std=c++17 -Wall -Wextra -I test/fakes -I src test/test_drivers.cpp test/fakes/fake_board.cpp \
//     src/drivers/{servos,button,scale,light,pir}.cpp -o /tmp/test_drivers && /tmp/test_drivers
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <string>

#include "Arduino.h"
#include "ESP32Servo.h"
#include "clock.h"
#include "config.h"
#include "drivers/button.h"
#include "drivers/light.h"
#include "drivers/pir.h"
#include "drivers/scale.h"
#include "drivers/servos.h"
#include "fake_board.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    checks++;                                                       \
    if (!(cond)) {                                                  \
      failures++;                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

static const uint32_t STARTS[] = {1000, 0x7FFFF000u, 0xFFFFF000u};  // plain, 2^31, 2^32

// --- servos ---

static void testRotorCapAcrossWraps() {
  for (uint32_t t0 : STARTS) {
    fake::setMillis(t0);
    servosBegin();
    CHECK(rotorSet(ROT_FWD));
    uint32_t start = millis();
    servosTick(start + ROTOR_MAX_RUN_MS - 10);
    CHECK(!rotorTripped() && rotorUs() == ROT_FWD);
    servosTick(start + ROTOR_MAX_RUN_MS + 10);
    CHECK(rotorTripped() && rotorUs() == ROT_STOP);
    CHECK(fake::log.find("SAFETY STOP") != std::string::npos);
    CHECK(!rotorSet(ROT_FWD));  // latched until an operator clears it
    rotorClearTrip();
    CHECK(rotorSet(ROT_FWD));
    rotorSet(ROT_STOP);
  }
}

static void testRotorStartedMidTickDoesNotTrip() {
  // the old code tripped at once here: rotorSet() stamped millis() a moment after the tick's now
  fake::setMillis(5000);
  servosBegin();
  uint32_t now = millis();
  fake::setMillis(now + 1);
  rotorSet(ROT_FWD);
  servosTick(now);
  CHECK(!rotorTripped() && rotorUs() == ROT_FWD);
}

static void testRotorDetachesWhenIdle() {
  fake::setMillis(1000);
  servosBegin();
  rotorSet(ROT_FWD);
  CHECK(fake::servo(PIN_ROTOR).attached && fake::servo(PIN_ROTOR).us == ROT_FWD);
  uint32_t stop = millis();
  rotorSet(ROT_STOP);
  servosTick(stop + ROTOR_IDLE_DETACH_MS - 10);
  CHECK(fake::servo(PIN_ROTOR).attached);
  servosTick(stop + ROTOR_IDLE_DETACH_MS + 10);
  CHECK(!fake::servo(PIN_ROTOR).attached);  // the FS90R creeps at 1500: detached...
  CHECK(fake::pin(PIN_ROTOR) == LOW);       // ...and held low
  int attaches = fake::servo(PIN_ROTOR).attaches;
  rotorSet(ROT_STOP);  // a stopped rotor stays detached
  CHECK(fake::servo(PIN_ROTOR).attaches == attaches);
}

static void testDoorStepsAndSaves() {
  fake::store[Key::Door] = ANG_CLOSED;
  fake::setMillis(1000);
  servosBegin();
  uint32_t now = millis();
  doorMoveTo(ANG_OPEN, false);
  CHECK(doorMoving() && fake::servo(PIN_LID).attached && fake::servo(PIN_LID).angle == ANG_CLOSED);
  int writes = fake::storeWrites;
  int maxGap = 0, lastSaved = ANG_CLOSED;
  for (int i = 0; i < 1000 && doorMoving(); i++) {
    now += CONTROL_TICK_MS;
    servosTick(now);
    if (fake::store[Key::Door] != lastSaved) lastSaved = fake::store[Key::Door];
    int gap = lastSaved - doorAngle();
    if (gap > maxGap) maxGap = gap;
  }
  CHECK(!doorMoving() && doorAngle() == ANG_OPEN && !doorClosed());
  CHECK(fake::store[Key::Door] == ANG_OPEN);
  CHECK(maxGap <= DOOR_SAVE_EVERY_DEG);  // a power cut mid-move leaves NVS this close
  int saves = fake::storeWrites - writes;
  CHECK(saves >= (ANG_CLOSED - ANG_OPEN) / DOOR_SAVE_EVERY_DEG && saves <= (ANG_CLOSED - ANG_OPEN) / 2);
  // one degree per STEP_MS after the attach pause
  uint32_t took = now - 1000;
  CHECK(took >= (uint32_t)(ANG_CLOSED - ANG_OPEN) * STEP_MS);
  doorMoveTo(ANG_CLOSED, true);  // close and release
  for (int i = 0; i < 1000 && doorMoving(); i++)
    servosTick(now += CONTROL_TICK_MS);
  CHECK(doorClosed() && !fake::servo(PIN_LID).attached && fake::pin(PIN_LID) == LOW);
}

static void testDoorResumesFromSavedAngle() {
  fake::store[Key::Door] = 120;  // a power cut left it half open
  fake::setMillis(1000);
  servosBegin();
  CHECK(doorAngle() == 120);
  doorMoveTo(ANG_CLOSED, false);
  CHECK(fake::servo(PIN_LID).angle == 120);  // attaches where it is: no slam
  CHECK(fake::log.find("left at 120") != std::string::npos);
}

static void testSafeStateStopsEverything() {
  fake::setMillis(1000);
  servosBegin();
  rotorSet(ROT_FWD);
  doorMoveTo(ANG_OPEN, false);
  servosTick(millis() + 500);
  servosSafe();
  CHECK(rotorUs() == ROT_STOP && !fake::servo(PIN_ROTOR).attached && fake::pin(PIN_ROTOR) == LOW);
  CHECK(!doorMoving() && !fake::servo(PIN_LID).attached && fake::pin(PIN_LID) == LOW);
  CHECK(fake::store[Key::Door] == doorAngle());  // stopped mid-move: remembered
}

// --- button ---

// down for 50 ms, then up for 700 ms; true if counted. The release is an edge too and starts
// its own debounce, so a press needs the release's debounce to pass before the next one.
static bool press(uint32_t& now) {
  bool counted = false;
  fake::setPin(PIN_BTN, LOW);
  for (int i = 0; i < 5; i++)
    counted |= buttonTick(now += CONTROL_TICK_MS);
  fake::setPin(PIN_BTN, HIGH);
  for (int i = 0; i < 70; i++)
    counted |= buttonTick(now += CONTROL_TICK_MS);
  return counted;
}

static void testButtonDebounces() {
  buttonBegin();
  fake::setPin(PIN_BTN, HIGH);
  uint32_t now = 1000;
  CHECK(press(now));
  // a bouncing contact: four edges within the debounce count once
  int count = 0;
  for (int i = 0; i < 4; i++) {
    fake::setPin(PIN_BTN, i % 2 ? HIGH : LOW);
    count += buttonTick(now += CONTROL_TICK_MS);
  }
  CHECK(count == 1);
}

static void testButtonAfterWeeksIdle() {
  // the regression: a debounce deadline polled only on an edge went stale after ~25 idle days
  for (uint32_t t0 : STARTS) {
    buttonBegin();
    fake::setPin(PIN_BTN, HIGH);
    uint32_t now = t0;
    CHECK(press(now));
    for (uint64_t t = 0; t < 30ull * 24 * 3600 * 1000; t += 997)  // 30 days of ticks (coarse)
      buttonTick(now += 997);
    int counted = 0;
    for (int i = 0; i < 20; i++)
      counted += press(now);
    CHECK(counted == 20);
  }
}

// --- scale ---

static int32_t rawFor(float grams, int32_t tare) { return tare + (int32_t)(grams * SCALE_CAL); }

static void testScaleBootTaresAnEmptyTray() {
  fake::hx.value = 84000;
  fake::setMillis(100);
  scaleBegin();
  CHECK(scaleOk() && scaleHealth() == ScaleHealth::Ready);
  CHECK(fake::store[Key::Tare] == 84000);
  fake::hx.value = rawFor(12.5f, 84000);
  float g = 0;
  CHECK(scaleTick(millis(), g));
  CHECK(g > 12.4f && g < 12.6f);
}

static void testScaleKeepsTareWhenTrayIsLoaded() {
  fake::store[Key::Tare] = 50000;
  fake::hx.value = rawFor(TRAY_LOADED_G + 15, 50000);  // food or the cat on the tray at boot
  fake::setMillis(100);
  scaleBegin();
  CHECK(fake::store[Key::Tare] == 50000);
  CHECK(fake::log.find("keeping the saved tare") != std::string::npos);
  float g = 0;
  CHECK(scaleTick(millis(), g) && g > TRAY_LOADED_G);  // reads the load, not 0
}

static void testScaleReadsNegativeSamples() {
  fake::hx.value = -200000 & 0xFFFFFF;  // 24-bit two's complement, as the chip sends it
  fake::setMillis(100);
  scaleBegin();
  CHECK(fake::store[Key::Tare] == -200000);
}

static void testScaleNeverWaitsAndGoesLost() {
  fake::hx.value = 1000;
  fake::setMillis(100);
  scaleBegin();
  fake::hx.present = false;  // the cable comes out
  uint32_t now = millis();
  float g = 0;
  for (uint32_t t = 0; t <= SCALE_LOST_MS + 100; t += CONTROL_TICK_MS) {
    uint32_t before = micros();
    CHECK(!scaleTick(now + t, g));
    CHECK(micros() - before < 100);  // a read without a chip returns at once
  }
  CHECK(scaleHealth() == ScaleHealth::Lost);
  CHECK(fake::log.find("scale LOST") != std::string::npos);
  fake::hx.present = true;  // plugged back in
  now += SCALE_LOST_MS + 100;
  for (uint32_t t = 0; t < SCALE_RETRY_MS + 2000 && !scaleOk(); t += CONTROL_TICK_MS)
    scaleTick(now += CONTROL_TICK_MS, g);
  CHECK(scaleOk());
  CHECK(fake::log.find("scale RECOVERED") != std::string::npos);
}

static void testScaleMissingAtBoot() {
  fake::hx.present = false;
  fake::setMillis(100);
  scaleBegin();
  CHECK(!scaleOk() && scaleHealth() == ScaleHealth::Missing);
  CHECK(fake::log.find("NOT CONNECTED") != std::string::npos);
}

// --- light ---

static void testLightRampsSoftly() {
  fake::setMillis(1000);
  lightBegin();
  lightSetPct(100);
  CHECK(fake::store[Key::Light] == 255);  // the "on" level survives a reboot
  uint32_t now = millis();
  lightOn();
  lightTick(now + LIGHT_RAMP_MS / 2);
  CHECK(lightLevel() > 60 && lightLevel() < 200);  // halfway, not a jump
  CHECK(fake::ledc[LED_CH] == lightLevel());
  lightTick(now + LIGHT_RAMP_MS + 10);
  CHECK(lightLevel() == 255);
  lightCut();
  CHECK(lightLevel() == 0 && fake::ledc[LED_CH] == 0);  // at once, no ramp
}

static void testLightRampStartedMidTick() {
  // the old code jumped straight to full: the ramp was stamped a moment after the tick's now
  fake::setMillis(1000);
  lightBegin();
  lightSetPct(100);
  uint32_t now = millis();
  fake::setMillis(now + 1);
  lightOn();
  lightTick(now);
  CHECK(lightLevel() < 20);
}

static void testLightAcrossWraps() {
  for (uint32_t t0 : STARTS) {
    fake::setMillis(t0);
    lightBegin();
    uint32_t now = millis();
    lightOn();
    lightTick(now + LIGHT_RAMP_MS + 10);
    CHECK(lightLevel() == lightMaxLevel());
  }
}

// --- PIR ---

static void testPirWarmupAndSelfTest() {
  pirBegin();
  CHECK(!pirReady(PIR_WARMUP_MS - 10));  // garbage right after power-up
  CHECK(pirReady(PIR_WARMUP_MS + 10));
  CHECK(pirReady(5));  // latched: a millis() wrap doesn't restart the warm-up
  fake::setPin(PIN_PIR, HIGH);
  pirTick(3000);
  CHECK(pirRaw());
  fake::floatPin(PIN_PIR);  // the module unplugged: the pin follows the pulls
  pirSelfTest(3000);        // pull-up, then pull-down: a free pin follows both
  for (uint32_t t = 3000; t < 3300; t += CONTROL_TICK_MS)
    pirTick(t);
  CHECK(fake::log.find("PIN OK") != std::string::npos);
  CHECK(fake::pinMode(PIN_PIR) == INPUT);  // back to a plain input
}

// --- runner: one process per test, so the drivers' statics start fresh ---

static void run(const char* name, void (*test)()) {
  std::printf("%s\n", name);
  std::fflush(stdout);
  int fds[2];
  if (pipe(fds) != 0) return;
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    checks = failures = 0;  // count this test only
    test();
    int counts[2] = {checks, failures};
    ssize_t w = write(fds[1], counts, sizeof(counts));
    std::fflush(stdout);
    _exit(w == (ssize_t)sizeof(counts) ? 0 : 1);
  }
  close(fds[1]);
  int counts[2] = {0, 1};
  ssize_t r = read(fds[0], counts, sizeof(counts));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (r != (ssize_t)sizeof(counts) || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::printf("  FAIL %s crashed\n", name);
    counts[0] = 1;
    counts[1] = 1;
  }
  checks += counts[0];
  failures += counts[1];
}

int main() {
  run("servos: the 20 s rotor cap fires on time across both wraps", testRotorCapAcrossWraps);
  run("servos: a rotor started mid-tick doesn't trip at once", testRotorStartedMidTickDoesNotTrip);
  run("servos: an idle rotor is detached and held low", testRotorDetachesWhenIdle);
  run("servos: the door steps one degree at a time and saves every few", testDoorStepsAndSaves);
  run("servos: the door attaches at its saved angle after a power cut", testDoorResumesFromSavedAngle);
  run("servos: the safe state stops the rotor and releases the door", testSafeStateStopsEverything);
  run("button: one press counts once, bounces don't", testButtonDebounces);
  run("button: presses still count after 30 idle days", testButtonAfterWeeksIdle);
  run("scale: boot tares an empty tray, then reads grams", testScaleBootTaresAnEmptyTray);
  run("scale: a loaded tray at boot keeps the saved tare", testScaleKeepsTareWhenTrayIsLoaded);
  run("scale: negative 24-bit samples are sign-extended", testScaleReadsNegativeSamples);
  run("scale: no chip never blocks, goes lost, recovers", testScaleNeverWaitsAndGoesLost);
  run("scale: no chip at boot is missing, not lost", testScaleMissingAtBoot);
  run("light: ramps softly, cuts at once, saves its level", testLightRampsSoftly);
  run("light: a ramp started mid-tick still ramps", testLightRampStartedMidTick);
  run("light: the ramp ends on time across both wraps", testLightAcrossWraps);
  run("pir: warm-up, reading and the self-test", testPirWarmupAndSelfTest);
  std::printf("\n%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}

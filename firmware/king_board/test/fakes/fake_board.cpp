// The link seam behind test/fakes/*.h: fake pins, clock, Serial, LEDC, an HX711 at the
// pin level, and stand-ins for core/log.cpp and core/store.cpp, so the drivers in
// src/drivers build and run on a PC unchanged.
#include "fake_board.h"
#include <cstdarg>
#include <cstdio>
#include "Arduino.h"
#include "ESP32Servo.h"
#include "HX711.h"
#include "config.h"
#include "core/log.h"
#include "core/store.h"

namespace fake {

static uint64_t nowUs = 0;
static std::map<int, int> levels, modes, inputs;
Hx711 hx;
std::map<Key, int32_t> store;
int storeWrites = 0;
std::string log;
std::map<int, int> ledc;

// the HX711 shifts out one bit per SCK rising edge, MSB first; the 25th edge ends the read
static int hxEdge = -1;  // -1: idle, 0..24: inside a read
static int hxBit = 1;

static std::map<int, ServoState> servos;
ServoState& servo(int p) { return servos[p]; }

void setMillis(uint32_t ms) { nowUs = (uint64_t)ms * 1000; }
void setPin(int p, int level) { inputs[p] = level; }
void floatPin(int p) { inputs.erase(p); }
int pin(int p) { return levels.count(p) ? levels[p] : -1; }
int pinMode(int p) { return modes.count(p) ? modes[p] : -1; }

static int hxDt() {
  if (!hx.present) return HIGH;  // pulled up, no chip
  if (hxEdge >= 0) return hxBit;
  return hx.ready ? LOW : HIGH;
}

static void hxClock(int level) {
  int before = levels.count(PIN_SC_SCK) ? levels[PIN_SC_SCK] : LOW;
  if (!(before == LOW && level == HIGH) || !hx.present) return;  // rising edges only
  if (hxEdge < 0) {
    if (!hx.ready) return;
    hxEdge = 0;
  }
  if (hxEdge < 24) hxBit = (int)(((uint32_t)hx.value >> (23 - hxEdge)) & 1u);
  if (++hxEdge == 25) {  // gain pulse: the read is over
    hxEdge = -1;
  }
}

}  // namespace fake

using namespace fake;

void pinMode(int p, int mode) { modes[p] = mode; }
void digitalWrite(int p, int level) {
  if (p == PIN_SC_SCK) hxClock(level);
  levels[p] = level;
}
int digitalRead(int p) {
  if (p == PIN_SC_DT) return hxDt();
  if (inputs.count(p)) return inputs[p];
  return modes.count(p) && modes[p] == INPUT_PULLUP ? HIGH : LOW;
}
uint32_t millis() {
  nowUs += 10;
  return (uint32_t)(nowUs / 1000);
}
uint32_t micros() { return (uint32_t)nowUs; }
void delay(uint32_t ms) { nowUs += (uint64_t)ms * 1000; }
void delayMicroseconds(uint32_t us) { nowUs += us; }
bool ledcAttachChannel(int, uint32_t, uint8_t, int) { return true; }
bool ledcWriteChannel(int channel, uint32_t duty) {
  fake::ledc[channel] = (int)duty;
  return true;
}

FakeSerial Serial;
void FakeSerial::println(const char* s) { (fake::log += s) += "\n"; }
void FakeSerial::print(const char* s) { fake::log += s; }

bool HX711::wait_ready_timeout(unsigned long ms) {
  if (hxDt() == LOW) return true;
  delay((uint32_t)ms);
  return hxDt() == LOW;
}

// core/log.cpp
void logBegin() {}
void say(const char* m) { (fake::log += m) += "\n"; }
static void vsay(const char* mark, const char* fmt, va_list ap) {
  char line[LOG_LINE_LEN];
  size_t n = (size_t)snprintf(line, sizeof(line), "%s", mark);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  say(line);
}
void sayf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay("", fmt, ap);
  va_end(ap);
}
void warnf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay("! ", fmt, ap);
  va_end(ap);
}
void errorf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay("!! ", fmt, ap);
  va_end(ap);
}

// core/store.cpp
void storeBegin() {}
void storeRun() {}
int32_t storeGet(Key k, int32_t fallback) { return store.count(k) ? store[k] : fallback; }
bool storeHas(Key k) { return store.count(k) != 0; }
void storePut(Key k, int32_t v) {
  if (store.count(k) && store[k] == v) return;
  store[k] = v;
  storeWrites++;
}
uint32_t storeStackFree() { return 0; }

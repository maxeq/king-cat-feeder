#pragma once
// A link seam for the driver tests: the drivers compile unchanged against this instead of
// the ESP32 core (g++ -I test/fakes). It fakes only what the drivers use: pins, time,
// Serial and the LEDC calls, and records what they did. Implemented in fake_board.cpp.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const int LOW = 0, HIGH = 1;
const int INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2, INPUT_PULLDOWN = 3, OUTPUT_OPEN_DRAIN = 4;

void pinMode(int pin, int mode);
void digitalWrite(int pin, int level);
int digitalRead(int pin);
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
bool ledcAttachChannel(int pin, uint32_t freq, uint8_t bits, int channel);
bool ledcWriteChannel(int channel, uint32_t duty);

template <typename T>
T constrain(T v, T lo, T hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

struct FakeSerial {
  void println(const char* s);
  void print(const char* s);
};
extern FakeSerial Serial;

// FreeRTOS critical sections (scale.cpp): nothing to guard on a PC
struct portMUX_TYPE {
  int unused;
};
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)

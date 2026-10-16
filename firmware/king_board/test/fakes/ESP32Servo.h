#pragma once
// Fake ESP32Servo for the driver tests: each servo records its state by pin, so a test
// can see what the driver did (fake::servo(PIN_ROTOR).attached, .us, .angle).
#include <cstdint>

namespace fake {
struct ServoState {
  bool attached = false;
  int us = 0, angle = -1, attaches = 0;
};
ServoState& servo(int pin);
}  // namespace fake

struct ESP32PWM {
  static void allocateTimer(int) {}
};

class Servo {
 public:
  bool attach(int pin, int, int) {
    pin_ = pin;
    fake::servo(pin).attached = true;
    fake::servo(pin).attaches++;
    return true;
  }
  void detach() {
    if (pin_ >= 0) fake::servo(pin_).attached = false;
  }
  void writeMicroseconds(int us) {
    if (pin_ >= 0) fake::servo(pin_).us = us;
  }
  void write(int angle) {
    if (pin_ >= 0) fake::servo(pin_).angle = angle;
  }

 private:
  int pin_ = -1;
};

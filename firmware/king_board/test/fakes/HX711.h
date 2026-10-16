#pragma once
// Fake HX711 library for the driver tests. The driver reads samples itself by clocking
// SCK and reading DT (readRaw), so the chip is simulated in fake_board.cpp at the pin
// level; this class only holds the offset and scale, and waits on the simulated DT pin.
#include <cstdint>

class HX711 {
 public:
  void begin(int, int, bool, bool) {}
  bool wait_ready_timeout(unsigned long ms);
  void set_scale(float s) { scale_ = s; }
  float get_scale() const { return scale_; }
  void set_offset(long o) { offset_ = o; }
  long get_offset() const { return offset_; }

 private:
  float scale_ = 1;
  long offset_ = 0;
};

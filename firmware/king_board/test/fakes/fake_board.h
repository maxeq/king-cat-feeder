#pragma once
// What the driver tests control and observe through the fakes: the clock, pin levels,
// the simulated HX711, the NVS store and the log.
#include <cstdint>
#include <map>
#include <string>
#include "types.h"

namespace fake {

void setMillis(uint32_t ms);      // millis() then creeps 10 us per call, so busy loops end
void setPin(int pin, int level);  // drive an input (the button)
void floatPin(int pin);           // nothing drives it: it follows its pull-up or pull-down
int pin(int pin);                 // the last level the driver wrote
int pinMode(int pin);

struct Hx711 {
  bool present = true;  // false: DT floats high (cable out)
  bool ready = true;    // a conversion is waiting
  int32_t value = 0;    // the 24-bit sample it will clock out
};
extern Hx711 hx;

extern std::map<Key, int32_t> store;
extern int storeWrites;
extern std::string log;
extern std::map<int, int> ledc;  // the last duty written per LEDC channel

}  // namespace fake

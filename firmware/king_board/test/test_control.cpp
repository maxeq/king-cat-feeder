// Host-side tests for the control glue (src/app/controller.cpp) against fake hardware,
// and for the Deadline timer every driver uses. Build and run:
//   g++ -std=c++17 -Wall -Wextra -I src test/test_control.cpp src/app/controller.cpp src/logic/*.cpp \
//     -o /tmp/test_control && /tmp/test_control
#include <cstdio>
#include <map>
#include <string>

#include "app/controller.h"
#include "clock.h"
#include "config.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    checks++;                                                       \
    if (!(cond)) {                                                  \
      failures++;                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

// The board as the controller sees it: a hopper that feeds while the rotor runs forward,
// a scale with 10 readings per second, a door that moves at once, and call records.
// One shared state, and one small fake per port that reads and writes it.
struct FakeBoard;

struct FakeRotor : Rotor {
  FakeBoard& b;
  explicit FakeRotor(FakeBoard& board) : b(board) {}
  bool set(int us) override;
  void clearTrip() override;
  bool tripped() override;
  int us() override;
};
struct FakeDoor : Door {
  FakeBoard& b;
  explicit FakeDoor(FakeBoard& board) : b(board) {}
  void moveTo(int angle, bool release) override;
  void release() override;
  int angle() override;
  bool closed() override;
  bool moving() override;
};
struct FakeServos : Servos {
  FakeBoard& b;
  explicit FakeServos(FakeBoard& board) : b(board) {}
  void safe() override;
  void tick(uint32_t) override {}
};
struct FakeScale : Scale {
  FakeBoard& b;
  explicit FakeScale(FakeBoard& board) : b(board) {}
  bool tick(uint32_t t, float& g) override;
  bool ok() override;
  ScaleHealth health() override;
  float grams() override;
  void tare() override {}
  void rawDump() override {}
};
struct FakeLight : Light {
  FakeBoard& b;
  explicit FakeLight(FakeBoard& board) : b(board) {}
  void on() override;
  void off() override;
  void cut() override;
  void setPct(int pct) override;
  void tick(uint32_t) override {}
  int level() override;
  int maxLevel() override;
};
struct FakeIndicator : Indicator {
  FakeBoard& b;
  explicit FakeIndicator(FakeBoard& board) : b(board) {}
  void set(IndicatorMode m) override;
  void tick(uint32_t) override {}
};
struct FakeInputs : Inputs {
  FakeBoard& b;
  explicit FakeInputs(FakeBoard& board) : b(board) {}
  bool buttonTick(uint32_t) override;
  void pirTick(uint32_t) override {}
  bool pirReady(uint32_t) override { return true; }
  bool pir() override;
  void pirSelfTest(uint32_t) override {}
};
struct FakePeers : Peers {
  FakeBoard& b;
  explicit FakePeers(FakeBoard& board) : b(board) {}
  bool cameraOnline() override;
  bool cameraBusy() override;
  bool bootCrashed() override;
};
struct FakeStore : Store {
  FakeBoard& b;
  explicit FakeStore(FakeBoard& board) : b(board) {}
  int32_t load(Key k, int32_t fallback) override;
  void save(Key k, int32_t v) override;
};
struct FakeLog : Log {
  FakeBoard& b;
  explicit FakeLog(FakeBoard& board) : b(board) {}
  void line(const char* text) override;
};

struct FakeBoard {
  uint32_t now = 0;
  int rotor = ROT_STOP;
  bool tripped = false;
  int door = ANG_CLOSED;
  bool doorRel = false, moving = false;
  int light = 0, lightMax = 51;
  IndicatorMode indicator = IndicatorMode::Off;
  bool buttonOnce = false, pir = false, online = true, crashed = false;
  ScaleHealth health = ScaleHealth::Ready;
  float grams = 50, rate = 3;
  uint32_t nextSample = 0;
  bool camBusy = false;
  std::map<Key, int32_t> store;
  std::string logged;
  int safeCalls = 0, cutCalls = 0, scaleTicks = 0, buttonTicks = 0;
  int actuatorCalls = 0;  // anything that can move or light something

  FakeRotor rotorPort{*this};
  FakeDoor doorPort{*this};
  FakeServos servosPort{*this};
  FakeScale scalePort{*this};
  FakeLight lightPort{*this};
  FakeIndicator indicatorPort{*this};
  FakeInputs inputsPort{*this};
  FakePeers peersPort{*this};
  FakeStore storePort{*this};
  FakeLog logPort{*this};

  Ports ports() {
    return {rotorPort,     doorPort,   servosPort, scalePort, lightPort,
            indicatorPort, inputsPort, peersPort,  storePort, logPort};
  }
  bool doorClosed() const { return door > (ANG_CLOSED + ANG_OPEN) / 2; }
  void feed() {  // one tick of hopper physics
    if (rotor > ROT_STOP) grams += rate * (float)(rotor - ROT_STOP) / 200.0f * CONTROL_TICK_MS / 1000.0f;
  }
};

bool FakeRotor::set(int us) {
  b.actuatorCalls += us != ROT_STOP;
  if (b.tripped && us != ROT_STOP) return false;
  b.rotor = us;
  return true;
}
void FakeRotor::clearTrip() { b.tripped = false; }
bool FakeRotor::tripped() { return b.tripped; }
int FakeRotor::us() { return b.rotor; }
void FakeDoor::moveTo(int angle, bool release) {
  b.actuatorCalls++;
  b.door = angle;
  b.doorRel = release;
}
void FakeDoor::release() { b.doorRel = true; }
int FakeDoor::angle() { return b.door; }
bool FakeDoor::closed() { return b.doorClosed(); }
bool FakeDoor::moving() { return b.moving; }
void FakeServos::safe() {
  b.safeCalls++;
  b.rotor = ROT_STOP;
  b.doorRel = true;
}
bool FakeScale::tick(uint32_t t, float& g) {
  b.scaleTicks++;
  if (b.health != ScaleHealth::Ready || !due(t, b.nextSample)) return false;
  b.nextSample = t + 100;
  g = b.grams;
  return true;
}
bool FakeScale::ok() { return b.health == ScaleHealth::Ready; }
ScaleHealth FakeScale::health() { return b.health; }
float FakeScale::grams() { return b.grams; }
void FakeLight::on() {
  b.actuatorCalls++;
  b.light = b.lightMax;
}
void FakeLight::off() { b.light = 0; }
void FakeLight::cut() {
  b.cutCalls++;
  b.light = 0;
}
void FakeLight::setPct(int pct) {
  b.actuatorCalls++;
  b.lightMax = pct * 255 / 100;
}
int FakeLight::level() { return b.light; }
int FakeLight::maxLevel() { return b.lightMax; }
void FakeIndicator::set(IndicatorMode m) { b.indicator = m; }
bool FakeInputs::buttonTick(uint32_t) {
  b.buttonTicks++;
  bool pressed = b.buttonOnce;
  b.buttonOnce = false;
  return pressed;
}
bool FakeInputs::pir() { return b.pir; }
bool FakePeers::cameraOnline() { return b.online; }
bool FakePeers::cameraBusy() { return b.camBusy; }
bool FakePeers::bootCrashed() { return b.crashed; }
int32_t FakeStore::load(Key k, int32_t fallback) { return b.store.count(k) ? b.store[k] : fallback; }
void FakeStore::save(Key k, int32_t v) { b.store[k] = v; }
void FakeLog::line(const char* text) { (b.logged += text) += "\n"; }

struct Rig {
  FakeBoard hw;
  Controller ctl{hw.ports()};
  explicit Rig(uint32_t start = 0) {
    hw.now = start;
    hw.nextSample = start;
    ctl.begin(start);
  }
  void tick() {
    ctl.tick(hw.now);
    ctl.tickTook(100, hw.now);
    hw.feed();
    hw.now += CONTROL_TICK_MS;
  }
  void run(uint32_t ms) {  // counts ticks, so it works across the millis() wrap
    for (uint32_t t = 0; t < ms; t += CONTROL_TICK_MS)
      tick();
  }
  void send(Cmd c, int arg = 0) { ctl.apply(c, arg, hw.now); }
};

// --- Deadline: the timer behind every driver deadline ---

static void testDeadlineAcrossWraps() {
  std::printf("deadline and elapsed(): on time across both wraps\n");
  const uint32_t starts[] = {0, 0x7FFFFF00u, 0xFFFFFF00u};
  for (uint32_t t0 : starts) {
    Deadline d;
    CHECK(!d.armed() && !d.expired(t0));
    d.start(t0, 500);
    CHECK(d.armed());
    CHECK(d.running(t0 + 499));
    CHECK(!d.expired(t0 + 499));
    CHECK(d.expired(t0 + 501));   // | 1 can move it by a millisecond
    CHECK(!d.expired(t0 + 502));  // once only
    CHECK(!d.armed());
    // a fired deadline stays quiet forever, even when millis() comes round again
    CHECK(!d.expired(t0 + 0x80000000u) && !d.expired(t0 + 0xFFFFFFFFu) && !d.running(t0 + 400));
  }
  // durations: the rotor cap, the light ramp and the scale timeout all measure with elapsed()
  for (uint32_t t0 : starts) {
    CHECK(elapsed(t0 + ROTOR_MAX_RUN_MS + 1, t0) > ROTOR_MAX_RUN_MS);  // the cap fires on time
    CHECK(elapsed(t0 + 150, t0) == 150);                               // the ramp is halfway
    CHECK(elapsed(t0 + SCALE_LOST_MS, t0) == SCALE_LOST_MS);
    CHECK(elapsed(t0, t0 + 1) == 0);  // stamped 1 ms after the tick's `now`: not 49.7 days
  }
  CHECK(elapsed(5, 0xFFFFFFFBu) == 10);  // across the 2^32 wrap
  // polled every tick (as the drivers do), a deadline stays quiet through months of idling
  Deadline idle;
  idle.start(0, BUTTON_DEBOUNCE_MS);
  bool woke = false;
  for (uint64_t t = 0; t < 0x100000000ull + 1000000; t += 997)  // past both wraps
    woke |= idle.running((uint32_t)t) && t > BUTTON_DEBOUNCE_MS + 1;
  CHECK(!woke);
  Deadline z;
  z.start(0xFFFFFFFFu, 1);  // (now + ms) == 0 would read "not armed" without the | 1
  CHECK(z.armed());
  CHECK(z.expired(1));
}

// --- the controller ---

static void testHaltDropsEveryCommand() {
  std::printf("control: while halted, no command moves or lights anything\n");
  Rig r;
  r.run(100);
  r.ctl.halt();
  CHECK(r.hw.safeCalls == 1 && r.hw.cutCalls == 1);
  int before = r.hw.actuatorCalls;
  const Cmd all[] = {Cmd::AutoOn,      Cmd::RotorFwd,  Cmd::RotorBack, Cmd::RotorPulse,  Cmd::DoorOpen,
                     Cmd::DoorClose,   Cmd::DoorNudge, Cmd::LightOn,   Cmd::LightAlways, Cmd::LightPct,
                     Cmd::IndicatorOn, Cmd::RotorStop, Cmd::Tare,      Cmd::PirSelfTest};
  for (Cmd c : all)
    r.send(c, 500);
  r.hw.buttonOnce = true;
  int scaleBefore = r.hw.scaleTicks;
  r.run(1000);
  CHECK(r.hw.actuatorCalls == before);
  CHECK(r.hw.rotor == ROT_STOP && r.hw.light == 0);
  CHECK(r.hw.scaleTicks == scaleBefore);  // the tick does nothing while halted
  CHECK(!r.ctl.status(r.hw.now).autoMode);
  CHECK(has(r.ctl.faultWord(r.hw.now), Fault::Halted));
  CHECK(r.hw.store.count(Key::LightAlways) == 0);  // no NVS writes either
  r.ctl.resume();
  r.send(Cmd::RotorFwd);
  CHECK(r.hw.rotor == ROT_FWD);
  CHECK(!has(r.ctl.faultWord(r.hw.now), Fault::Halted));
}

static void testHaltAbortsADose() {
  std::printf("control: a halt mid-dose stops it dead, with no 'interrupted' report\n");
  Rig r;
  r.run(100);
  r.hw.buttonOnce = true;
  r.run(2000);
  CHECK(r.ctl.status(r.hw.now).dosing);
  CHECK(r.hw.store[Key::DoseRunning] == 1);
  r.ctl.halt();
  CHECK(!r.ctl.status(r.hw.now).dosing);
  CHECK(r.hw.rotor == ROT_STOP);
  CHECK(r.hw.store[Key::DoseRunning] == 0);
  r.ctl.resume();
  r.run(5000);
  CHECK(r.hw.rotor == ROT_STOP);
  CHECK(!r.ctl.status(r.hw.now).dosing);
}

static void testPulseAcrossWraps() {
  std::printf("control: a console pulse stops on time across both wraps\n");
  const uint32_t starts[] = {1000, 0x7FFFFF00u, 0xFFFFFF00u};
  for (uint32_t t0 : starts) {
    Rig r(t0);
    r.send(Cmd::RotorPulse, 300);
    CHECK(r.hw.rotor == ROT_FWD);
    r.run(290);
    CHECK(r.hw.rotor == ROT_FWD);
    r.run(30);
    CHECK(r.hw.rotor == ROT_STOP);
    r.run(0x2000);
    CHECK(r.hw.logged.find("rotor: STOP") == r.hw.logged.rfind("rotor: STOP"));  // once
  }
}

static void testPulseRefusedWhileDosing() {
  std::printf("control: manual motor commands wait for a dose\n");
  Rig r;
  r.hw.buttonOnce = true;
  r.run(500);
  CHECK(r.ctl.status(r.hw.now).dosing);
  int door = r.hw.door;
  r.send(Cmd::DoorClose);
  r.send(Cmd::RotorPulse, 300);
  CHECK(r.hw.door == door);
  CHECK(r.hw.logged.find("busy: dosing") != std::string::npos);
}

static void testOverrunFaultClearsForGood() {
  std::printf("control: TickOverrun holds for a while, then never comes back by itself\n");
  Rig r(1000);
  uint32_t t0 = r.hw.now;
  r.ctl.tickTook(CONTROL_TICK_MS * 1000 + 1, t0);
  CHECK(r.ctl.status(t0).overruns == 1);
  CHECK(has(r.ctl.faultWord(t0 + 10), Fault::TickOverrun));
  r.tick();
  CHECK(r.hw.indicator == IndicatorMode::Blink);  // a critical fault
  CHECK(has(r.ctl.faultWord(t0 + TICK_OVERRUN_HOLD_MS - 10), Fault::TickOverrun));
  CHECK(!has(r.ctl.faultWord(t0 + TICK_OVERRUN_HOLD_MS + 10), Fault::TickOverrun));
  // 49.7 days later millis() reads t0 again: the old code raised the fault a second time here
  CHECK(!has(r.ctl.faultWord(t0 + 5), Fault::TickOverrun));
  r.hw.now = t0 + TICK_OVERRUN_HOLD_MS + 20;
  r.tick();
  CHECK(r.hw.indicator == IndicatorMode::On);
  CHECK(r.ctl.status(r.hw.now).tickMaxUs == CONTROL_TICK_MS * 1000 + 1);
}

static void testFaultWordFollowsTheHardware() {
  std::printf("control: each fault bit comes from its own signal\n");
  Rig r;
  CHECK(r.ctl.faultWord(r.hw.now) == 0);
  r.hw.health = ScaleHealth::Missing;
  CHECK(r.ctl.faultWord(r.hw.now) == mask(Fault::ScaleMissing));
  r.hw.health = ScaleHealth::Lost;
  CHECK(r.ctl.faultWord(r.hw.now) == mask(Fault::ScaleLost));
  r.hw.health = ScaleHealth::Ready;
  r.hw.tripped = true;
  CHECK(r.ctl.faultWord(r.hw.now) == mask(Fault::RotorTripped));
  r.hw.tripped = false;
  r.hw.online = false;
  CHECK(r.ctl.faultWord(r.hw.now) == mask(Fault::GroveOffline));
  r.hw.online = true;
  r.hw.crashed = true;
  CHECK(r.ctl.faultWord(r.hw.now) == mask(Fault::BootCrashed));
  r.hw.crashed = false;
  r.hw.health = ScaleHealth::Taring;
  CHECK(has(r.ctl.stateWord(), State::ScaleTaring));
  r.hw.camBusy = true;
  CHECK(has(r.ctl.stateWord(), State::CameraBusy));
}

static void testButtonClearsATrip() {
  std::printf("control: the button re-arms a latched rotor stop\n");
  Rig r;
  r.hw.tripped = true;
  r.send(Cmd::RotorFwd);
  CHECK(r.hw.rotor == ROT_STOP);
  CHECK(r.hw.logged.find("refused") != std::string::npos);
  r.hw.buttonOnce = true;
  r.tick();
  CHECK(!r.hw.tripped);
}

static void testNudgeStaysInRange() {
  std::printf("control: door nudges stay inside the calibration range\n");
  Rig r;
  r.hw.door = ANG_NUDGE_MAX - 2;
  r.send(Cmd::DoorNudge, +5);
  CHECK(r.hw.door == ANG_NUDGE_MAX);
  r.hw.door = ANG_NUDGE_MIN + 2;
  r.send(Cmd::DoorNudge, -5);
  CHECK(r.hw.door == ANG_NUDGE_MIN);
  r.send(Cmd::LightPct, 250);
  CHECK(r.hw.lightMax == 255);
}

static void testBootRestoresAndReports() {
  std::printf("control: boot restores the light mode, reports a lost dose, keeps auto off\n");
  FakeBoard hw;
  hw.store[Key::LightAlways] = 1;
  hw.store[Key::DoseRunning] = 1;
  Controller ctl(hw.ports());
  ctl.begin(0);
  CHECK(hw.light > 0);
  CHECK(hw.logged.find("dose was running") != std::string::npos);
  CHECK(hw.store[Key::DoseRunning] == 0);
  CHECK(!ctl.status(0).autoMode);  // never feeds on its own after a reboot
  ctl.apply(Cmd::LightMotion, 0, 0);
  CHECK(hw.store[Key::LightAlways] == 0);
}

static void testDoseThroughTheController() {
  std::printf("control: a button dose runs to the target and remembers it is running\n");
  Rig r;
  r.hw.buttonOnce = true;
  r.run(1000);
  CHECK(r.hw.store[Key::DoseRunning] == 1);
  CHECK(has(r.ctl.stateWord(), State::Dosing));
  r.run(60000);
  Status s = r.ctl.status(r.hw.now);
  CHECK(!s.dosing && s.alarm == feeder::Alarm::None);
  CHECK(r.hw.store[Key::DoseRunning] == 0);
  CHECK(r.hw.rotor == ROT_STOP);
  CHECK(!r.hw.doorClosed());  // opened for the cat
  CHECK(s.valid && s.dispensed >= DOSE_TARGET_G - TARGET_TOL_G);
}

// --- randomized: commands, halts and presses in any order, at any time ---

struct Lcg {
  uint32_t state;
  explicit Lcg(uint32_t seed) : state(seed * 2654435761u + 1) {}
  uint32_t next() { return state = state * 1664525u + 1013904223u; }
  bool chance(float p) { return (float)(next() >> 8) / float(1 << 24) < p; }
};

static void testRandomizedCommands() {
  const int RUNS = 500;
  std::printf("randomized control: %d runs\n", RUNS);
  for (int seed = 1; seed <= RUNS; seed++) {
    Lcg rnd((uint32_t)seed);
    Rig r(rnd.chance(0.5f) ? 0xFFFFFFFFu - (rnd.next() % 200000) : rnd.next() % 1000);
    bool ok = true;
    auto expect = [&](bool cond, const char* what) {
      if (!cond && ok) {
        failures++;
        ok = false;
        std::printf("  FAIL seed %d: %s\n", seed, what);
      }
    };
    for (int i = 0; i < 20000; i++) {  // 200 s
      if (rnd.chance(0.01f)) r.send((Cmd)(rnd.next() % ((int)Cmd::PirSelfTest + 1)), (int)(rnd.next() % 900));
      if (rnd.chance(0.0005f)) r.hw.buttonOnce = true;
      if (rnd.chance(0.002f)) r.hw.pir = !r.hw.pir;
      if (rnd.chance(0.0005f)) r.ctl.halt();
      if (rnd.chance(0.001f)) r.ctl.resume();
      bool halted = r.ctl.halted();
      int before = r.hw.actuatorCalls;
      r.tick();
      expect(!halted || r.hw.actuatorCalls == before, "an actuator moved while halted");
      expect(!halted || r.hw.rotor == ROT_STOP, "rotor running while halted");
      expect(!r.ctl.halted() || has(r.ctl.faultWord(r.hw.now), Fault::Halted),
             "halted without the fault bit");
    }
    r.ctl.resume();
    r.send(Cmd::AutoOff);
    r.send(Cmd::RotorStop);
    r.run(DOSE_TIMEOUT + 10000);  // a dose, a pulse: everything ends on its own
    expect(r.hw.rotor == ROT_STOP, "rotor left running with nothing to do");
    expect(!r.ctl.status(r.hw.now).dosing, "a dose outlived its timeout");
    checks++;
  }
}

int main() {
  testDeadlineAcrossWraps();
  testHaltDropsEveryCommand();
  testHaltAbortsADose();
  testPulseAcrossWraps();
  testPulseRefusedWhileDosing();
  testOverrunFaultClearsForGood();
  testFaultWordFollowsTheHardware();
  testButtonClearsATrip();
  testNudgeStaysInRange();
  testBootRestoresAndReports();
  testDoseThroughTheController();
  testRandomizedCommands();
  std::printf("\n%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}

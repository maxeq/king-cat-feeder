// Host-side tests for src/logic: the dose controller and the feeder state machine,
// driven by a simulated rotor and scale. Build and run:
//   g++ -std=c++17 -Wall -Wextra -I src test/test_logic.cpp src/logic/*.cpp -o /tmp/test_logic &&
//   /tmp/test_logic
#include <cstdio>
#include <functional>
#include <string>

#include "config.h"
#include "logic/dose.h"
#include "logic/feeder.h"
#include "logic/health.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    checks++;                                                       \
    if (!(cond)) {                                                  \
      failures++;                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

// A hopper that delivers `rate` grams per second while the rotor feeds forward,
// read by an HX711 at 10 samples per second.
struct Sim {
  uint32_t now = 0;
  float grams = 50;  // tray weight
  float rate = 3;    // g/s while feeding
  bool scaleDead = false;
  uint32_t deadFrom = UINT32_MAX;
  uint32_t nextSample = 0;
  bool started = false;
  int rotorUs = ROT_STOP;
  std::string log;

  dose::Sample sample() {
    dose::Sample s;
    if (!started || (int32_t)(now - nextSample) >= 0) {  // wrap-safe, for tests near 2^32
      started = true;
      nextSample = now + 100;
      if (!scaleDead && (deadFrom == UINT32_MAX || (int32_t)(now - deadFrom) < 0)) {
        s.fresh = true;
        s.grams = grams;
      }
    }
    return s;
  }
  void advance() {
    if (rotorUs > ROT_STOP) grams += rate * (rotorUs - ROT_STOP) / 200.0f * CONTROL_TICK_MS / 1000.0f;
    now += CONTROL_TICK_MS;
  }
  void keep(const Messages& m) {
    for (int i = 0; i < m.count; i++)
      (log += m.line[i]) += "\n";
  }
};

static dose::Result runDose(Sim& sim, bool timed = false,
                            std::function<void(Sim&, dose::Controller&)> hook = {},
                            dose::Params params = dose::Params()) {
  dose::Controller c(params);
  if (timed)
    c.startTimed(sim.now);
  else
    c.startWeighed(sim.now, sim.grams);
  for (int i = 0; i < 20000; i++) {  // 200 s cap
    if (hook) hook(sim, c);
    dose::Output o = c.step(sim.now, sim.sample());
    sim.keep(o.log);
    sim.rotorUs = o.rotorUs;
    if (o.result != dose::Result::Running) return o.result;
    sim.advance();
  }
  return dose::Result::Running;
}

static void testReachesTarget() {
  std::printf("dose reaches target\n");
  Sim sim;
  float start = sim.grams;
  CHECK(runDose(sim) == dose::Result::Reached);
  CHECK(sim.grams - start >= DOSE_TARGET_G - 0.5f);
  CHECK(sim.grams - start < DOSE_TARGET_G + 3);  // no big overshoot
  CHECK(sim.rotorUs == ROT_STOP);
  CHECK(sim.log.find("TARGET REACHED") != std::string::npos);
}

static void testOpensDoorOnce() {
  std::printf("dose asks for the door once, after the shake\n");
  Sim sim;
  dose::Controller c;
  c.startWeighed(0, sim.grams);
  int opens = 0;
  uint32_t firstOpen = 0;
  for (int i = 0; i < 20000 && c.running(); i++) {
    dose::Output o = c.step(sim.now, sim.sample());
    if (o.openDoor && !opens++) firstOpen = sim.now;
    sim.rotorUs = o.rotorUs;
    sim.advance();
  }
  CHECK(opens == 1);
  CHECK(firstOpen >= 3000);  // 3 s of shaking first
}

static void testJamEscalates() {
  std::printf("jam escalates through all levels, then gives up\n");
  Sim sim;
  sim.rate = 0;
  CHECK(runDose(sim) == dose::Result::Jammed);
  CHECK(sim.log.find("escalation 1") != std::string::npos);
  CHECK(sim.log.find("escalation 2") != std::string::npos);
  CHECK(sim.log.find("escalation 3") != std::string::npos);
  CHECK(sim.rotorUs == ROT_STOP);
}

static void testScaleFaultStopsWithoutEscalation() {
  std::printf("scale dies mid-dose: stop, no escalation\n");
  Sim sim;
  sim.rate = 0.2f;
  sim.deadFrom = 5000;
  CHECK(runDose(sim) == dose::Result::ScaleFault);
  CHECK(sim.log.find("escalation 1") == std::string::npos);
  CHECK(sim.rotorUs == ROT_STOP);
  CHECK(sim.now < 20000);  // stops on the first failed weigh
}

static void testCancel() {
  std::printf("cancel stops the rotor\n");
  Sim sim;
  auto hook = [](Sim& s, dose::Controller& c) {
    if (s.now == 4000) c.cancel();
  };
  CHECK(runDose(sim, false, hook) == dose::Result::Cancelled);
  CHECK(sim.rotorUs == ROT_STOP);
  CHECK(sim.now < 4000 + SETTLE_MS + WEIGH_WINDOW_MS + 100);
}

static void testTimeout() {
  std::printf("slow trickle hits the timeout\n");
  Sim sim;
  sim.rate = 0.4f;  // ~1.2 g per cycle: flowing, but slow
  dose::Params p;
  p.timeout = 20000;
  CHECK(runDose(sim, false, {}, p) == dose::Result::Timeout);
  CHECK(sim.now >= p.timeout);
  CHECK(sim.now < p.timeout + SETTLE_MS + WEIGH_WINDOW_MS + 100);
  CHECK(sim.log.find("escalation 1") == std::string::npos);
}

static void testScaleDiesAtTheTimeout() {
  std::printf("a scale that goes quiet at the timeout is a scale fault, not 'no food'\n");
  Sim sim;
  sim.rate = 0.4f;  // slow enough to reach the timeout
  dose::Params p;
  p.timeout = 20000;
  sim.deadFrom = p.timeout - 200;  // dies just before the final weigh
  CHECK(runDose(sim, false, {}, p) == dose::Result::ScaleFault);
  CHECK(sim.rotorUs == ROT_STOP);
}

static void testCatOnTrayIsNotFood() {
  std::printf("a cat stepping on mid-feed pauses the rotor, not a false 'reached'\n");
  Sim sim;
  float start = sim.grams;
  int pausedTicks = 0;
  auto hook = [&](Sim& s, dose::Controller&) {
    if (s.now == 6000) s.grams += 30;   // cat steps on
    if (s.now == 12000) s.grams -= 30;  // and off again
    if (s.now > 6200 && s.now < 12000 && s.rotorUs == ROT_STOP) pausedTicks++;
  };
  CHECK(runDose(sim, false, hook) == dose::Result::Reached);
  CHECK(sim.log.find("jump +") != std::string::npos);
  CHECK(pausedTicks > 400);                          // the rotor waited while the cat was there
  CHECK(sim.grams - start >= DOSE_TARGET_G - 0.5f);  // real food, not the cat's weight
}

static void testTimedDose() {
  std::printf("timed dose runs 8 s without a scale\n");
  Sim sim;
  sim.scaleDead = true;
  CHECK(runDose(sim, true) == dose::Result::TimedDone);
  CHECK(sim.now >= 8000 && sim.now < 8100);
}

static void testCatInTwoSteps() {
  std::printf("a cat stepping on paw by paw is not a reached target\n");
  Sim sim;
  sim.rate = 0.5f;
  float food = 0;
  auto hook = [&](Sim& s, dose::Controller&) {
    if (s.now == 6000 || s.now == 6300) s.grams += 12;  // 12 g, then 24 g: each step under the jump limit
    if (s.now == 15000) s.grams -= 24;
  };
  dose::Controller c;
  c.startWeighed(0, sim.grams);
  dose::Result r = dose::Result::Running;
  for (int i = 0; i < 30000 && r == dose::Result::Running; i++) {
    hook(sim, c);
    dose::Output o = c.step(sim.now, sim.sample());
    sim.keep(o.log);
    r = o.result;
    float before = sim.grams;
    sim.rotorUs = o.rotorUs;
    sim.advance();
    food += sim.grams - before;
  }
  CHECK(r == dose::Result::Reached);  // the paws left at 15 s, so the dose finishes on real food
  CHECK(food >= DOSE_TARGET_G - 1.0f);
  CHECK(sim.log.find("jump +") != std::string::npos);
}

// runs a weighed dose with a hook that sees the food delivered so far
static dose::Result runWithFood(Sim& sim, float& food, std::function<void(Sim&, float)> hook) {
  dose::Controller c;
  c.startWeighed(sim.now, sim.grams);
  dose::Result r = dose::Result::Running;
  for (int i = 0; i < 30000 && r == dose::Result::Running; i++) {
    hook(sim, food);
    dose::Output o = c.step(sim.now, sim.sample());
    sim.keep(o.log);
    r = o.result;
    float before = sim.grams;
    sim.rotorUs = o.rotorUs;
    sim.advance();
    food += sim.grams - before;
  }
  return r;
}

static void testPawDuringConfirm() {
  std::printf("a light paw during the confirm weigh doesn't stall the dose\n");
  Sim sim;
  float food = 0;
  uint32_t pawOff = 0;
  auto hook = [&](Sim& s, float f) {
    if (!pawOff && f >= DOSE_TARGET_G) {  // the target is in: a paw lands during the confirm
      s.grams += 4;
      pawOff = s.now + 2800;
    }
    if (pawOff && s.now == pawOff) s.grams -= 4;
  };
  CHECK(runWithFood(sim, food, hook) == dose::Result::Reached);
  CHECK(sim.now < 40000);  // no 90 s stall
}

static void testCatOnDuringFinalWeigh() {
  std::printf("a cat landing during the final weigh is not the target\n");
  Sim sim;
  sim.rate = 1.5f;
  float food = 0;
  uint32_t leaves = 0;
  auto hook = [&](Sim& s, float f) {
    if (!leaves && f >= DOSE_TARGET_G - 0.5f && s.rotorUs == ROT_STOP) {
      s.grams += 40;
      leaves = s.now + 6000;
    }
    if (leaves && s.now == leaves) s.grams -= 40;
  };
  CHECK(runWithFood(sim, food, hook) == dose::Result::Reached);
  CHECK(food >= DOSE_TARGET_G - 1.0f);
  CHECK(sim.now > leaves);  // it waited for the cat
}

static void testTimedDoseIgnoresARecoveredScale() {
  std::printf("a timed dose ignores a scale that comes back under load\n");
  Sim sim;
  sim.deadFrom = 0;  // dead at the start...
  dose::Controller c;
  c.startTimed(0);
  dose::Result r = dose::Result::Running;
  for (int i = 0; i < 5000 && r == dose::Result::Running; i++) {
    if (sim.now == 3000) {
      sim.deadFrom = UINT32_MAX;  // ...back at 3 s with 40 g on the tray
      sim.grams += 40;
    }
    dose::Output o = c.step(sim.now, sim.sample());
    r = o.result;
    sim.rotorUs = o.rotorUs;
    sim.advance();
  }
  CHECK(r == dose::Result::TimedDone);
  CHECK(sim.now >= 8000 && sim.now < 8100);
}

static void testDoseAcrossTheMillisWrap() {
  std::printf("doses work across both millis() wraps\n");
  const uint32_t starts[] = {0x7FFFF000u, 0xFFFFC000u};
  for (uint32_t start : starts) {
    Sim sim;
    sim.now = start;
    CHECK(runDose(sim) == dose::Result::Reached);
    CHECK(sim.rotorUs == ROT_STOP);
    Sim jam;
    jam.now = start;
    jam.rate = 0;
    CHECK(runDose(jam) == dose::Result::Jammed);
  }
}

static void testKnockDuringShakeKeepsTheDoor() {
  std::printf("a knock during the shake still opens the door\n");
  Sim sim;
  dose::Controller c;
  c.startWeighed(0, sim.grams);
  bool opened = false;
  for (int i = 0; i < 2000; i++) {
    if (sim.now == 1500) sim.grams += 20;
    if (sim.now == 1700) sim.grams -= 20;
    dose::Output o = c.step(sim.now, sim.sample());
    opened |= o.openDoor;
    sim.rotorUs = o.rotorUs;
    sim.advance();
  }
  CHECK(opened);
}

static void testCatUntilTimeout() {
  std::printf("a cat that never leaves: cat blocked, not 'no food', one log line\n");
  Sim sim;
  dose::Params p;
  p.timeout = 30000;
  auto hook = [](Sim& s, dose::Controller&) {
    if (s.now == 5000) s.grams += 40;
  };
  CHECK(runDose(sim, false, hook, p) == dose::Result::CatBlocked);
  size_t first = sim.log.find("jump +");
  CHECK(first != std::string::npos && sim.log.find("jump +", first + 1) == std::string::npos);
  CHECK(sim.rotorUs == ROT_STOP);
}

// --- randomized doses: invariants that must hold whatever the hopper and the cat do ---

struct Lcg {  // deterministic, so a failing seed can be replayed
  uint32_t state;
  explicit Lcg(uint32_t seed) : state(seed * 2654435761u + 1) {}
  uint32_t next() { return state = state * 1664525u + 1013904223u; }
  float uniform(float lo, float hi) { return lo + (hi - lo) * (next() >> 8) / float(1 << 24); }
  bool chance(float p) { return uniform(0, 1) < p; }
};

static void testRandomizedDoses() {
  const int RUNS = 10000;
  std::printf("randomized doses: %d runs\n", RUNS);
  int reached = 0, jammed = 0, faults = 0, cancelled = 0, timeouts = 0, catBlocked = 0;
  for (int seed = 1; seed <= RUNS; seed++) {
    Lcg rnd(seed);
    Sim sim;
    sim.rate = rnd.chance(0.2f) ? 0 : rnd.uniform(0.1f, 6);  // some hoppers are empty or jammed
    float food = 0;                                          // what the rotor really delivered
    uint32_t catOn = rnd.chance(0.3f) ? (uint32_t)rnd.uniform(4000, 60000) : UINT32_MAX;
    uint32_t catOff = catOn == UINT32_MAX ? UINT32_MAX : catOn + (uint32_t)rnd.uniform(500, 15000);
    float catWeight = rnd.uniform(24, 60);  // at least 8 g per paw: kibble never jumps that fast
    int catStages = 1 + rnd.next() % 3;
    catOn = catOn == UINT32_MAX ? catOn : catOn / 10 * 10;
    catOff = catOff == UINT32_MAX ? catOff : catOff / 10 * 10 + 1000;
    bool scaleDies = rnd.chance(0.15f);
    sim.deadFrom = scaleDies ? (uint32_t)rnd.uniform(3000, 80000) : UINT32_MAX;
    uint32_t cancelAt = rnd.chance(0.1f) ? (uint32_t)rnd.uniform(1000, 60000) / 10 * 10 : UINT32_MAX;
    // a light paw, under the step detector: 2-6 g for a moment
    bool paws = rnd.chance(0.3f);
    float pawWeight = rnd.uniform(2, 6);
    uint32_t pawOn = paws ? (uint32_t)rnd.uniform(4000, 80000) / 10 * 10 : UINT32_MAX;
    uint32_t pawOff = paws ? pawOn + (uint32_t)rnd.uniform(300, 3000) / 10 * 10 : UINT32_MAX;
    // a load that creeps on in small steps, each under the step detector
    bool creeps = rnd.chance(0.15f);
    int creepSteps = 2 + rnd.next() % 4;
    float creepStep = rnd.uniform(1.5f, 3.0f);
    uint32_t creepOn = creeps ? (uint32_t)rnd.uniform(4000, 85000) / 10 * 10 : UINT32_MAX;
    uint32_t creepOff =
        creeps ? creepOn + creepSteps * 400 + (uint32_t)rnd.uniform(500, 8000) / 10 * 10 : UINT32_MAX;
    // sometimes the cat arrives right around the timeout
    if (catOn != UINT32_MAX && rnd.chance(0.2f)) {
      catOn = (uint32_t)rnd.uniform(DOSE_TIMEOUT - 3000, DOSE_TIMEOUT + 1500) / 10 * 10;
      catOff = catOn + 20000;
    }

    dose::Controller c;
    float start = sim.grams;
    c.startWeighed(0, start);
    int opens = 0, lastUs = ROT_STOP;
    uint32_t sameSince = 0, longestRun = 0;
    bool overflow = false;
    dose::Result r = dose::Result::Running;
    for (int i = 0; i < 30000 && r == dose::Result::Running; i++) {
      for (int k = 0; k < catStages; k++)  // the cat steps on paw by paw, 300 ms apart
        if (sim.now == catOn + 300u * k) sim.grams += catWeight / catStages;
      if (sim.now == catOff) sim.grams -= catWeight;
      for (int k = 0; k < creepSteps && creeps; k++)
        if (sim.now == creepOn + 400u * k) sim.grams += creepStep;
      if (sim.now == creepOff) sim.grams -= creepStep * creepSteps;
      if (sim.now == pawOn) sim.grams += pawWeight;
      if (sim.now == pawOff) sim.grams -= pawWeight;
      if (sim.now == cancelAt) c.cancel();
      dose::Output o = c.step(sim.now, sim.sample());
      r = o.result;
      opens += o.openDoor;
      overflow |= o.log.count == Messages::MAX;
      if (o.rotorUs != lastUs) {
        lastUs = o.rotorUs;
        sameSince = sim.now;
      } else if (o.rotorUs != ROT_STOP && sim.now - sameSince > longestRun) {
        longestRun = sim.now - sameSince;
      }
      float before = sim.grams;
      sim.rotorUs = o.rotorUs;
      sim.advance();
      food += sim.grams - before;
    }
    bool ok = true;
    auto expect = [&](bool cond, const char* what) {
      checks++;
      if (!cond && ok) {
        failures++;
        ok = false;
        std::printf("  FAIL seed %d: %s (result %s, t=%u)\n", seed, what, dose::resultName(r), sim.now);
      }
    };
    expect(r != dose::Result::Running, "dose never ended");
    expect(sim.now <= DOSE_TIMEOUT + SETTLE_MS + WEIGH_WINDOW_MS + 200, "ran past the timeout");
    expect(lastUs == ROT_STOP, "rotor left running");
    expect(opens <= 1, "door opened more than once");
    expect(longestRun <= ROTOR_MAX_RUN_MS, "one rotor command ran longer than the driver cap");
    // a steady paw through both confirm weighs is indistinguishable from food: allow for it
    float pawNow = (paws && sim.now >= pawOn && sim.now < pawOff + 1000) ? pawWeight : 0;
    if (creeps && sim.now >= creepOn && sim.now < creepOff + 1000) pawNow += creepStep * creepSteps;
    bool catNow = catOn != UINT32_MAX && sim.now >= catOn && sim.now < catOff;
    expect(r != dose::Result::Reached || food >= DOSE_TARGET_G - 1.0f - pawNow,
           "reached on the cat's weight");
    expect(r != dose::Result::Timeout || food < DOSE_TARGET_G + 1.0f,
           "timeout with a full tray: false alarm");
    expect(r != dose::Result::ScaleFault || scaleDies, "scale fault with a healthy scale");
    expect(r != dose::Result::CatBlocked || catNow || pawNow > 0, "cat blocked with nothing on the tray");
    expect(r != dose::Result::Reached || !catNow, "reached with the cat on the tray");
    expect(!overflow, "a step filled the log buffer, lines may be lost");
    // dead through the whole last weigh: the scale can't have seen a timeout or a jam
    bool deadAtEnd = scaleDies && (int32_t)(sim.now - sim.deadFrom) > (int32_t)(SETTLE_MS + WEIGH_WINDOW_MS);
    expect(!deadAtEnd || (r != dose::Result::Timeout && r != dose::Result::Jammed),
           "a dead scale reported as no food");
    reached += r == dose::Result::Reached;
    jammed += r == dose::Result::Jammed;
    faults += r == dose::Result::ScaleFault;
    cancelled += r == dose::Result::Cancelled;
    timeouts += r == dose::Result::Timeout;
    catBlocked += r == dose::Result::CatBlocked;
  }
  std::printf("  reached %d, jammed %d, scale fault %d, cancelled %d, timeout %d, cat blocked %d\n", reached,
              jammed, faults, cancelled, timeouts, catBlocked);
  // every outcome was exercised, so none of them can silently stop happening
  CHECK(reached > 0 && jammed > 0 && faults > 0 && cancelled > 0 && timeouts > 0 && catBlocked > 0);
}

// --- feeder FSM ---

struct Bench {
  feeder::Fsm fsm;
  Sim sim;
  bool doorClosed = true, doorMoving = false;
  int light = 0;
  feeder::Indicator indicator = feeder::Indicator::None;
  std::string log;

  feeder::Outputs tick(bool press = false, bool pir = false) {
    feeder::Inputs in;
    in.now = sim.now;
    in.buttonPressed = press;
    in.pirReady = sim.now > PIR_WARMUP_MS;
    in.pir = pir;
    in.doorClosed = doorClosed;
    in.doorMoving = doorMoving;
    in.scaleOk = !sim.scaleDead;
    in.grams = sim.grams;
    in.sample = sim.sample();
    feeder::Outputs o = fsm.step(in);
    if (o.rotorUs >= 0) sim.rotorUs = o.rotorUs;
    if (o.door == feeder::Door::Open) doorClosed = false;
    if (o.door == feeder::Door::Close || o.door == feeder::Door::CloseAndRelease) doorClosed = true;
    if (o.light == feeder::Light::On) light = 51;
    if (o.light == feeder::Light::Off) light = 0;
    if (o.indicator != feeder::Indicator::None) indicator = o.indicator;
    for (int i = 0; i < o.log.count; i++)
      (log += o.log.line[i]) += "\n";
    sim.advance();
    return o;
  }
  void run(uint32_t ms, bool pir = false) {  // counts ticks, so it works across the millis() wrap
    for (uint32_t t = 0; t < ms; t += CONTROL_TICK_MS)
      tick(false, pir);
  }
};

static void testButtonDoseThenClose() {
  std::printf("fsm: button doses and opens, next press closes\n");
  Bench b;
  b.fsm.setAuto(false);
  b.run(3000);
  b.tick(true);
  CHECK(b.fsm.dosing());
  b.run(60000);
  CHECK(!b.fsm.dosing());
  CHECK(!b.doorClosed);
  CHECK(b.fsm.alarm() == feeder::Alarm::None);
  CHECK(b.indicator == feeder::Indicator::On);
  b.tick(true);
  CHECK(b.doorClosed);
}

static void testButtonCancels() {
  std::printf("fsm: second press cancels, no alarm\n");
  Bench b;
  b.fsm.setAuto(false);
  b.run(3000);
  b.tick(true);
  b.run(5000);
  b.tick(true);
  b.run(3000);
  CHECK(!b.fsm.dosing());
  CHECK(b.fsm.alarm() == feeder::Alarm::None);
  CHECK(b.sim.rotorUs == ROT_STOP);
}

static void testJamRaisesAlarm() {
  std::printf("fsm: jam raises the no-food alarm\n");
  Bench b;
  b.fsm.setAuto(false);
  b.sim.rate = 0;
  b.run(3000);
  b.tick(true);
  b.run(DOSE_TIMEOUT + 5000);
  CHECK(b.fsm.alarm() == feeder::Alarm::NoFood);
  CHECK(b.indicator == feeder::Indicator::Blink);
}

static void testAutoModePulse() {
  std::printf("fsm: auto mode opens and pulses on motion, closes on quiet\n");
  Bench b;
  b.fsm.setAuto(true);
  b.run(3000);
  b.tick(false, true);
  CHECK(!b.doorClosed);
  CHECK(b.light > 0);
  CHECK(b.sim.rotorUs == ROT_FWD);
  b.run(AUTO_PULSE_MS + 50, true);
  CHECK(b.sim.rotorUs == ROT_STOP);
  b.run(PIR_BLIND_MS + 50, false);
  b.tick(false, false);
  CHECK(b.doorClosed);
  CHECK(b.light == 0);
}

static void testMotionLight() {
  std::printf("fsm: light follows motion when auto is off\n");
  Bench b;
  b.fsm.setAuto(false);
  b.run(3000);
  b.tick(false, true);
  CHECK(b.light > 0);
  b.run(MOTION_LIGHT_MS + 100, false);
  CHECK(b.light == 0);
}

static void testAlarmClearsIndicator() {
  std::printf("fsm: a cancel after an alarm turns the indicator back on\n");
  Bench b;
  b.fsm.setAuto(false);
  b.sim.rate = 0;
  b.run(3000);
  b.tick(true);
  b.run(DOSE_TIMEOUT + 5000);
  CHECK(b.indicator == feeder::Indicator::Blink);
  b.sim.rate = 3;
  b.doorClosed = true;
  b.tick(true);
  b.run(4000);
  b.tick(true);  // cancel
  b.run(3000);
  CHECK(b.fsm.alarm() == feeder::Alarm::None);
  CHECK(b.indicator == feeder::Indicator::On);
}

static void testTimedDoseKeepsScaleAlarm() {
  std::printf("fsm: a timed dose after a scale fault keeps the alarm\n");
  Bench b;
  b.fsm.setAuto(false);
  b.sim.deadFrom = 5000;
  b.run(3000);
  b.tick(true);
  b.run(30000);
  CHECK(b.fsm.alarm() == feeder::Alarm::ScaleFault);
  b.sim.scaleDead = true;
  b.doorClosed = true;
  b.tick(true);  // no scale: timed dose
  b.run(12000);
  CHECK(b.fsm.alarm() == feeder::Alarm::ScaleFault);
}

static void testPirIgnoredWhileDosing() {
  std::printf("fsm: PIR edges during a dose don't close the door in auto mode\n");
  Bench b;
  b.fsm.setAuto(true);
  b.sim.rate = 0.3f;  // slow hopper: the dose is still going during the flicker
  b.run(3000);
  b.tick(true);
  b.run(5000);  // door opened by the dose
  CHECK(!b.doorClosed);
  for (int i = 0; i < 20; i++)
    b.run(200, i % 2);  // vibration flicker
  CHECK(!b.doorClosed);
  CHECK(b.fsm.dosing());
}

static void testPressWhileDoorMoves() {
  std::printf("fsm: a press while the door moves does nothing\n");
  Bench b;
  b.fsm.setAuto(false);
  b.run(3000);
  b.doorMoving = true;
  b.tick(true);
  CHECK(!b.fsm.dosing());
  CHECK(b.log.find("door still moving") != std::string::npos);
}

static void testFsmLongUptime() {
  std::printf("fsm: PIR light still works after 25 days of uptime\n");
  Bench b;
  b.fsm.setAuto(false);
  b.sim.now = 0x80000000u + 10000;  // past the signed half of millis()
  b.run(1000);
  b.tick(false, true);
  CHECK(b.light > 0);
}

static void testFsmNearTheFullWrap() {
  std::printf("fsm: a dose and the PIR light work across the 2^32 wrap\n");
  Bench b;
  b.fsm.setAuto(false);
  b.sim.now = 0xFFFFFFFFu - 20000;  // the dose crosses the wrap
  b.run(1000);
  b.tick(true);
  CHECK(b.fsm.dosing());
  b.run(60000);
  CHECK(!b.fsm.dosing());
  CHECK(b.fsm.alarm() == feeder::Alarm::None);
  CHECK(b.sim.rotorUs == ROT_STOP);
  b.tick(false, true);
  CHECK(b.light > 0);
  b.run(MOTION_LIGHT_MS + 100, false);
  CHECK(b.light == 0);
}

static void testMotionLightAfterLongAuto() {
  std::printf("fsm: the motion light still turns off after weeks in auto mode\n");
  Bench b;
  b.fsm.setAuto(false);
  b.run(3000);
  b.tick(false, true);       // motion: light on, off-deadline armed
  b.fsm.setAuto(true);       // auto takes over before that deadline is polled
  b.sim.now += 0x80000000u;  // ~24.8 days later
  b.fsm.setAuto(false);
  b.tick(false, true);
  b.run(MOTION_LIGHT_MS + 100, false);
  CHECK(b.light == 0);
}

static void testSlowFlowIsNotAJam() {
  std::printf("fsm: a slow hopper raises 'slow flow', not 'no food'\n");
  Bench b;
  b.fsm.setAuto(false);
  b.sim.rate = 0.2f;  // too slow for a normal cycle, so it climbs the ladder and trickles until the timeout
  b.run(3000);
  b.tick(true);
  b.run(DOSE_TIMEOUT + 5000);
  CHECK(!b.fsm.dosing());
  CHECK(b.fsm.alarm() == feeder::Alarm::SlowFlow);
  CHECK(b.indicator == feeder::Indicator::Blink);
}

static void testEmptyHopperWithAPawIsNoFood() {
  std::printf("fsm: an empty hopper that hits the timeout is 'no food', not 'slow flow'\n");
  // nothing flows; the door takes 1.7 s and a paw sits on the tray at 30-32.5 s, which slows
  // the jam ladder just enough that the 90 s timeout ends the dose, with 0 g dispensed
  Bench b;
  b.fsm.setAuto(false);
  b.sim.rate = 0;
  b.run(3000);
  uint32_t t0 = b.sim.now, doorUntil = 0;
  b.tick(true);
  for (int i = 0; i < 20000 && b.fsm.dosing(); i++) {
    uint32_t t = b.sim.now - t0;
    if (t == 30000) b.sim.grams += 8;
    if (t == 32500) b.sim.grams -= 8;
    b.doorMoving = doorUntil && b.sim.now < doorUntil;
    bool wasClosed = b.doorClosed;
    b.tick();
    if (wasClosed && !b.doorClosed) doorUntil = b.sim.now + 1700;
  }
  CHECK(b.log.find("TIMEOUT") != std::string::npos);  // the scenario really reaches the timeout
  CHECK(b.fsm.alarm() == feeder::Alarm::NoFood);

  // the same with a light 3 g paw that lands at 86 s and is still there at the timeout
  Bench c;
  c.fsm.setAuto(false);
  c.sim.rate = 0;
  c.run(3000);
  t0 = c.sim.now;
  doorUntil = 0;
  c.tick(true);
  for (int i = 0; i < 20000 && c.fsm.dosing(); i++) {
    uint32_t t = c.sim.now - t0;
    if (t == 30000) c.sim.grams += 8;
    if (t == 32500) c.sim.grams -= 8;
    if (t == 86000) c.sim.grams += 3;
    c.doorMoving = doorUntil && c.sim.now < doorUntil;
    bool wasClosed = c.doorClosed;
    c.tick();
    if (wasClosed && !c.doorClosed) doorUntil = c.sim.now + 1700;
  }
  CHECK(c.log.find("TIMEOUT") != std::string::npos);
  CHECK(c.fsm.alarm() == feeder::Alarm::NoFood);
}

static void testAbortStopsAtOnce() {
  std::printf("fsm: abort drops a dose and an auto pulse at once\n");
  Bench b;
  b.fsm.setAuto(false);
  b.run(3000);
  b.tick(true);
  b.run(2000);
  CHECK(b.fsm.dosing());
  b.fsm.abort();
  feeder::Outputs o = b.tick();
  CHECK(!b.fsm.dosing());
  CHECK(o.rotorUs == -1 || o.rotorUs == ROT_STOP);
  Bench a;
  a.fsm.setAuto(true);
  a.run(3000);
  a.tick(false, true);
  CHECK(a.sim.rotorUs == ROT_FWD);
  a.fsm.abort();
  a.run(100, true);
  CHECK(a.tick(false, true).rotorUs != ROT_FWD);
}

// random presses, motion and door timing: the FSM must keep the rotor and door safe
static void testRandomizedFsm() {
  const int RUNS = 2000;
  std::printf("randomized fsm: %d runs\n", RUNS);
  for (int seed = 1; seed <= RUNS; seed++) {
    Lcg rnd(seed);
    Bench b;
    b.fsm.setAuto(rnd.chance(0.5f));
    b.sim.rate = rnd.chance(0.2f) ? 0 : rnd.uniform(0.2f, 5);
    if (rnd.chance(0.2f)) b.sim.deadFrom = (uint32_t)rnd.uniform(0, 60000);
    b.sim.now = rnd.chance(0.3f) ? 0xFFFFFFFFu - (uint32_t)rnd.uniform(0, 100000) : 0;
    uint32_t sameSince = b.sim.now, longest = 0;
    int lastUs = ROT_STOP;
    bool ok = true, pir = false;
    auto expect = [&](bool cond, const char* what) {  // one check per run, not per tick
      if (!cond && ok) {
        failures++;
        ok = false;
        std::printf("  FAIL seed %d: %s\n", seed, what);
      }
    };
    for (int i = 0; i < 30000; i++) {  // 300 s
      if (rnd.chance(0.002f)) pir = !pir;
      b.doorMoving = rnd.chance(0.01f);
      bool press = rnd.chance(0.0005f);
      bool wasDosing = b.fsm.dosing();
      feeder::Outputs o = b.tick(press, pir);
      if (b.sim.rotorUs != lastUs) {
        lastUs = b.sim.rotorUs;
        sameSince = b.sim.now;
      } else if (lastUs != ROT_STOP && b.sim.now - sameSince > longest) {
        longest = b.sim.now - sameSince;
      }
      expect(o.log.count < Messages::MAX, "a step filled the log buffer");
      expect(!(wasDosing && !b.fsm.dosing() && o.rotorUs != ROT_STOP), "a dose ended with the rotor running");
      expect(b.fsm.dosing() || !b.fsm.catOnTray(), "cat on tray reported outside a dose");
    }
    for (uint32_t t = 0; t < DOSE_TIMEOUT + 10000 && b.fsm.dosing(); t += CONTROL_TICK_MS)
      b.tick();  // let a dose started late run to its end
    expect(!b.fsm.dosing(), "a dose outlived its timeout");
    b.run(AUTO_PULSE_MS + 100);  // no dose, no motion: whatever ran must stop on its own
    expect(b.sim.rotorUs == ROT_STOP, "rotor left running with nothing to do");
    expect(longest <= ROTOR_MAX_RUN_MS, "one rotor command ran longer than the driver cap");
    checks++;
  }
}

static void testHealthWords() {
  std::printf("health words: bits, names, critical mask\n");
  uint16_t f = Fault::ScaleLost | Fault::RotorTripped;
  CHECK(f == 0x0006);
  CHECK(has(f, Fault::ScaleLost) && !has(f, Fault::NoFood));
  CHECK(CRITICAL_FAULTS & mask(Fault::NoFood));
  CHECK(!(CRITICAL_FAULTS & mask(Fault::WifiDown)));  // no Wi-Fi is not a reason to blink
  CHECK(!(CRITICAL_FAULTS & mask(Fault::GroveOffline)));
  for (int i = 0; i < 16; i++) {  // every bit in use has a name, and no name points at an unused bit
    CHECK((faultName(i) != nullptr) == (i < FAULT_BITS));
    CHECK((stateName(i) != nullptr) == (i < STATE_BITS));
  }
  CHECK(faultName(-1) == nullptr && faultName(16) == nullptr);
  CHECK(CRITICAL_FAULTS & mask(Fault::DoseTimeout));
  CHECK(std::string(faultName(1)) == "ScaleLost");
}

int main() {
  testReachesTarget();
  testOpensDoorOnce();
  testJamEscalates();
  testScaleFaultStopsWithoutEscalation();
  testCancel();
  testTimeout();
  testCatOnTrayIsNotFood();
  testTimedDose();
  testCatInTwoSteps();
  testPawDuringConfirm();
  testCatOnDuringFinalWeigh();
  testTimedDoseIgnoresARecoveredScale();
  testDoseAcrossTheMillisWrap();
  testKnockDuringShakeKeepsTheDoor();
  testCatUntilTimeout();
  testScaleDiesAtTheTimeout();
  testRandomizedDoses();
  testButtonDoseThenClose();
  testButtonCancels();
  testJamRaisesAlarm();
  testAutoModePulse();
  testMotionLight();
  testAlarmClearsIndicator();
  testTimedDoseKeepsScaleAlarm();
  testPirIgnoredWhileDosing();
  testPressWhileDoorMoves();
  testFsmLongUptime();
  testFsmNearTheFullWrap();
  testMotionLightAfterLongAuto();
  testSlowFlowIsNotAJam();
  testEmptyHopperWithAPawIsNoFood();
  testAbortStopsAtOnce();
  testRandomizedFsm();
  testHealthWords();
  std::printf("\n%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}

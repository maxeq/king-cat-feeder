---
name: embedded-rule-17-ports-adapters
description: Audit embedded C++ rule 17 on the King board firmware: the core reaches the outside world through narrow ports, wired in one place. Use when checking the architecture or before a release; read-only.
---

# Rule 17: the core talks to the world through narrow ports

**The rule.** The code that decides (`src/logic`, `src/app/controller.cpp`) reaches hardware, storage, time and the log only through ports: small abstract interfaces in pure headers. Adapters implement the ports (the drivers on the board, fakes in the tests). Concrete objects are created and wired together in one composition root (`src/app/control.cpp`, `king_board.ino`), and handed to the core through its constructor. The network layer sees only ports from `app/`, never a driver.

**Why it matters.** A port the core depends on is a seam: a fake can replace it, so the code behind it runs on a PC. A fat port forces every fake to implement everything, and a port that includes a driver header drags the hardware back into the tests. Wiring scattered across files hides who owns what.

## What to check in this repo

- **Ports are pure.** List the port headers (`src/app/ports.h`, `src/app/camera.h`). Their includes may be only `<stdint.h>`-style system headers, other port headers and `src/` root headers (`config.h`, `clock.h`, `types.h`). Check with `grep -n '#include' firmware/king_board/src/app/ports.h firmware/king_board/src/app/camera.h`. An include of `drivers/`, `core/` or `Arduino.h` makes the abstraction depend on an implementation.
- **Ports are narrow.** Count the pure virtual methods per port. Over about 10 in one interface, or a consumer that uses only a small part of what it is given, is a fat port. Name what the split would be (rotor, door, scale, light, sensors, store, clock, log).
- **The core calls no globals.** `grep -nE 'millis|micros|storePut|storeGet|say\(|sayf|digital|ledc|Wire|Serial|rotorSet|doorMoveTo|scaleTick|aiStatus' firmware/king_board/src/app/controller.cpp firmware/king_board/src/logic` must find nothing but comments: everything goes through the injected port.
- **One composition root.** Adapters are instantiated and wired only in `src/app/control.cpp` and `king_board.ino`. A driver that creates another module's object, or a module that reaches a global instance of another layer, is wiring outside the root.
- **Every port has a fake.** For each port, find its implementation in `firmware/king_board/test/` (`Fake<Port>` in `test/test_control.cpp`). A method added to a port without a fake breaks the host build; a port with no fake at all means its consumer is untested.
- **The network sees only `app/`.** `grep -n '#include' firmware/king_board/src/net/*.cpp firmware/king_board/src/net/*.h`: an include of `drivers/` is a port that is missing from `app/` (the camera goes through `app/camera.h`).
- **Adapters are testable.** A driver that holds safety or timing logic must run on a PC, by either of two seams: an object that takes its pins, time and storage from outside, or a link seam, where the driver compiles unchanged against fakes of the core and libraries (`test/fakes/`, used by `test/test_drivers.cpp`). Either counts; what matters is that the logic has host tests. List each driver with such logic (servos: rotor cap, idle detach, door steps; scale: tare, lost and recovered; button: debounce; light: ramp; pir: self-test) and whether a host test covers it. Trivial pin wrappers (indicator) need none.

## Verdict

PASS: the core reaches everything through pure, narrow ports with fakes, wiring lives in the composition root, and net sees only `app/`. PARTIAL: the core is clean (no global calls), but a port is fat or impure, net reaches a driver, or a driver with safety logic has no host test. FAIL: the core calls drivers, time or storage directly.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

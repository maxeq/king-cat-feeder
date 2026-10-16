---
name: embedded-rule-03-non-blocking
description: Audit embedded C++ rule 3 on the King board firmware: nothing blocks the control path. Use when checking the architecture or before a release; read-only.
---

# Rule 3: nothing blocks the control path

**The rule.** Code that moves motors or reads sensors never waits: no `delay()`, no busy loop, no blocking I/O. It advances a state machine one tick at a time.

**Why it matters.** A blocked loop can't stop a motor. A rotor that keeps feeding while the code waits on the network empties the hopper onto the floor.

## What to check in this repo

- `grep -rn 'delay(\|delayMicroseconds\|while *(' firmware/king_board/src/app firmware/king_board/src/drivers firmware/king_board/src/logic`. Every hit must be justified: setup-only, a bounded few microseconds, or inside a non-control task.
- In `src/app/control.cpp`, every call made from `tick()` must return quickly. Follow calls into drivers and libraries; HX711 `read()` is allowed only after `is_ready()`.
- Blocking work (camera, network) must run in another task: `src/core/worker.*` and `loop()`.

## Verdict

PASS: no blocking call is reachable from the control tick. PARTIAL: something bounded but long (over 1 ms) is reachable. FAIL: an unbounded wait is reachable.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

---
name: embedded-rule-11-persistent-state
description: Audit embedded C++ rule 11 on the King board firmware: critical state survives a reboot. Use when checking the architecture or before a release; read-only.
---

# Rule 11: critical state survives a reboot

**The rule.** Anything whose loss would cause a physical misstep after a power cut or crash is stored in non-volatile memory and restored on boot: actuator positions, calibration, tare. Updates can be rolled back.

**Why it matters.** A positional servo attaches at whatever angle the code believes in. If that belief is wrong, the door slams. A tare taken with food on the tray makes every reading wrong.

## What to check in this repo

- `src/core/store.*` is the NVS wrapper. Find every `storeGet`/`storePut` and list the keys.
- Door angle: saved after each move and on release, and restored before the first attach (`src/drivers/servos.cpp`).
- Tare: a loaded-tray check at boot and on reconnect (`src/drivers/scale.cpp` `applyTare`).
- NVS writes must not happen every tick (flash wear), and `storePut` should skip unchanged values.
- Rollback: `src/core/rollback.*` counts crashed boots and switches slots.
- What is not persisted that should be? For example, a dose in progress when power dropped.

## Verdict

PASS: positions, calibration and tare are persisted and restored safely, with rollback. PARTIAL: some state is not persisted. FAIL: nothing is persisted.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

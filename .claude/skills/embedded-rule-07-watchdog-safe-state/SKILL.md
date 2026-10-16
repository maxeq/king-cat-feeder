---
name: embedded-rule-07-watchdog-safe-state
description: Audit embedded C++ rule 7 on the King board firmware: watchdog on every task, and a defined safe state. Use when checking the architecture or before a release; read-only.
---

# Rule 7: watchdog on every task, and a defined safe state

**The rule.** Every long-running task feeds a watchdog, so a hang anywhere reboots the board. There is one safe state (motors stopped, outputs off) that the firmware enters before risky operations and after faults.

**Why it matters.** A hang with a motor running is the worst failure a feeder can have.

## What to check in this repo

- Each task (`control`, `loop`, every `Worker`) calls `watchdogSubscribe()` and feeds the watchdog on every path, including long loops. See `src/core/watchdog.*`.
- The OTA path unsubscribes during the write and resubscribes on error. See `src/net/web.cpp` `setupOTA`.
- The safe state is `servosSafe()` plus `lightCut()` via `Cmd::SafeStop`. Check that it really stops the rotor and releases the door, and that nothing re-arms them while halted.
- Driver safety limits don't depend on the logic: the rotor run cap and its latch in `src/drivers/servos.cpp`.

## Verdict

PASS: every task is watched, the safe state is complete, and limits live in the drivers. PARTIAL: a task is unwatched, or the safe state is incomplete. FAIL: no watchdog.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

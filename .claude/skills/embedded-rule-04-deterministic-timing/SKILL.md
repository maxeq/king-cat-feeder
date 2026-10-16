---
name: embedded-rule-04-deterministic-timing
description: Audit embedded C++ rule 4 on the King board firmware: timing is deterministic and measured. Use when checking the architecture or before a release; read-only.
---

# Rule 4: timing is deterministic and measured

**The rule.** The control loop runs at a fixed rate with a priority above everything that can wait, and the firmware measures whether each tick fits its slot.

**Why it matters.** Without a fixed rate, timing depends on load. Without measurement, a tick that slowly grows past its slot goes unnoticed until behaviour drifts.

## What to check in this repo

- The control task uses `vTaskDelayUntil`, not `delay`. Its priority is above the network and worker tasks. Check `src/app/control.cpp` and `src/core/worker.cpp`.
- Tick duration is measured, the slowest tick and an overrun count are kept, and an overrun sets a fault bit (`Fault::TickOverrun` in `src/logic/health.h`).
- These numbers are visible: `/health` in `src/net/web.cpp` and the console `?`.
- Logic durations are expressed in milliseconds from `config.h`, not in tick counts.

## Verdict

PASS: fixed rate, correct priority, overruns measured and reported. PARTIAL: fixed rate with no measurement. FAIL: free-running loop.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

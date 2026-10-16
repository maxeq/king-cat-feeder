---
name: embedded-rule-08-fault-handling
description: Audit embedded C++ rule 8 on the King board firmware: faults are explicit, and escalation needs evidence. Use when checking the architecture or before a release; read-only.
---

# Rule 8: faults are explicit, and escalation needs evidence

**The rule.** Faults are named states (a fault word, alarms), not scattered booleans. An alarm or escalation is raised only on positive evidence, and different causes give different faults.

**Why it matters.** "No food" when the scale is actually dead sends the owner to the wrong fix. Named faults also make telemetry and support possible.

## What to check in this repo

- `src/logic/health.h`: is every fault the firmware can detect a bit, and are the bits documented in the README?
- `src/app/controller.cpp` `faultWord()`: does each bit come from a real signal?
- `src/logic/dose.cpp`: do different causes stay distinct (Jammed, Timeout, ScaleFault, CatBlocked)? Does a missing measurement (scale silent) avoid triggering escalation?
- `src/logic/feeder.cpp` `onDoseDone`: are alarms set and cleared consistently, and does the indicator follow `CRITICAL_FAULTS`?

## Verdict

PASS: one fault word, distinct causes, evidence-based escalation. PARTIAL: faults exist but are scattered or conflated. FAIL: failures are silent.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

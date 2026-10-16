---
name: embedded-rule-10-central-config
description: Audit embedded C++ rule 10 on the King board firmware: configuration in one place, no magic numbers. Use when checking the architecture or before a release; read-only.
---

# Rule 10: configuration in one place, no magic numbers

**The rule.** Pins, timings, thresholds and calibration live in one config header with units in the names or comments. Code refers to them by name.

**Why it matters.** Tuning on the bench means changing numbers. Numbers hidden in five files get changed in four.

## What to check in this repo

- `firmware/king_board/src/config.h` holds pins and tuning constants.
- Find numeric literals in `src/logic`, `src/drivers` and `src/app` that are tuning values: durations, grams, thresholds, calibration. For example `CAL_SCALE` and `LOST_MS` in `scale.cpp`, the ladder segment durations in `dose.cpp`, and the debounce in `button.cpp`.
- Protocol constants, such as SSCMA command codes and I2C addresses, may stay in their driver if they are named.

## Verdict

PASS: every tuning value is in config.h or a named constant at the top of its module. PARTIAL: a few tuning literals are scattered. FAIL: magic numbers throughout.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

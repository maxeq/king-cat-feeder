---
name: embedded-rule-09-time-wraparound
description: Audit embedded C++ rule 9 on the King board firmware: time arithmetic survives millis() wraparound. Use when checking the architecture or before a release; read-only.
---

# Rule 9: time arithmetic survives millis() wraparound

**The rule.** `millis()` wraps after about 49.7 days. Durations use unsigned subtraction (`now - start`). Deadlines use a signed-difference helper. Deadlines that have fired are cleared, because a stale one reads as "not yet" again after about 24.8 days.

**Why it matters.** A feeder runs for months. A comparison like `now > deadline` freezes the PIR, button or door weeks after deployment.

## What to check in this repo

- `grep -rnE 'now *[<>]=? *[a-zA-Z_]+|millis\(\) *[<>]' firmware/king_board/src firmware/king_board/king_board.ino`. Each hit must be a duration check or use `due()` from `src/clock.h`.
- Each deadline variable must be cleared to 0 after it fires or be refreshed regularly, and must be set with `| 1` when 0 means "none".
- `test/test_logic.cpp` must have tests that start near 2^31 and 2^32. Run: `cd firmware/king_board && g++ -std=c++17 -I src test/test_logic.cpp src/logic/*.cpp -o /tmp/t && /tmp/t`.

## Verdict

PASS: all comparisons are wrap-safe, with tests across both wraps. PARTIAL: wrap-safe logic but untested drivers. FAIL: raw comparisons remain.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

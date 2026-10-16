---
name: embedded-rule-05-static-memory
description: Audit embedded C++ rule 5 on the King board firmware: no heap churn in steady state. Use when checking the architecture or before a release; read-only.
---

# Rule 5: no heap churn in steady state

**The rule.** After boot, the firmware avoids dynamic allocation in long-running paths. It uses fixed-size buffers and queues, and keeps large buffers in PSRAM.

**Why it matters.** Heap fragmentation is slow: the board runs for a week, then one allocation fails and it reboots or hangs.

## What to check in this repo

- Find `String`, `new`, `malloc`, `std::vector` and `std::string` in `firmware/king_board/src`. Separate setup-only uses from per-tick or per-request uses.
- `src/logic` should use fixed buffers, like `Messages` in `src/logic/messages.h`. Check for heap use there.
- In hot paths such as the control tick, `say()` and web handlers, estimate how often allocations happen and how big they are. Images in `src/drivers/grove.cpp` and `src/net/web.cpp` are the large ones.
- With PSRAM on (`sketch.yaml`), check that allocations above the threshold go to PSRAM.
- Check that free heap and the largest free block are reported (console `?`).

## Verdict

PASS: no allocation in the control tick and bounded allocation elsewhere, with heap health reported. PARTIAL: logic is fixed-size, but web, log or camera use String. FAIL: allocation in the control tick.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

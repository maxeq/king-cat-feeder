---
name: embedded-rule-06-timeouts
description: Audit embedded C++ rule 6 on the King board firmware: every wait has a timeout. Use when checking the architecture or before a release; read-only.
---

# Rule 6: every wait has a timeout

**The rule.** No wait is unbounded: bus transactions, sensor reads, network writes and queue receives all have a limit, and a watchdog backs them up.

**Why it matters.** One missing device or stalled peer must not freeze the board.

## What to check in this repo

- HX711: only `wait_ready_timeout` or `is_ready` before `read()`, never a bare `read()` or `tare()`. See `src/drivers/scale.cpp`.
- I2C: `Wire.setTimeOut`, and polling loops bounded by time. See `src/drivers/grove.cpp`.
- Network: telnet and web writes bounded, with slow clients dropped. See `src/net/console.cpp` and `src/net/web.cpp`.
- Queues: `xQueueReceive` with a finite timeout wherever the task must also feed a watchdog.
- `grep -rn 'portMAX_DELAY\|while *(' firmware/king_board/src` and justify each hit.

## Verdict

PASS: every wait is bounded. PARTIAL: a wait is bounded only by the watchdog. FAIL: an unbounded wait exists.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

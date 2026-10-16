---
name: embedded-rule-02-single-owner
description: Audit embedded C++ rule 2 on the King board firmware: every resource has exactly one owner. Use when checking the architecture or before a release; read-only.
---

# Rule 2: every resource has exactly one owner

**The rule.** Each piece of hardware (servo, scale, bus, LED, NVS) and each piece of mutable state has one owning task. Other tasks send it messages or read a published snapshot.

**Why it matters.** Two tasks driving the same servo or bus produce races that appear once a week and can't be reproduced.

## What to check in this repo

- List the tasks: `xTaskCreate*` calls, `loop()`, worker threads. Check `firmware/king_board/king_board.ino` and `src/core/worker.*`.
- For each driver in `src/drivers/`, find every caller with `grep -rn '<function>(' firmware/king_board/src firmware/king_board/king_board.ino`, and confirm that all callers run in one task.
- Shared data must be behind a lock (`src/core/lock.h`) or a queue. Look for file-scope variables written by one task and read by another without one.
- `setup()` may touch hardware before the tasks start. That is fine if nothing touches it after `controlBegin()` from the wrong task.

## Verdict

PASS: one owner per resource, and cross-task data only through queues or locked snapshots. PARTIAL: one or two benign shared reads, such as a bool. FAIL: two tasks write the same hardware or state.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

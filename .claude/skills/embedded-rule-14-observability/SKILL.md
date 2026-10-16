---
name: embedded-rule-14-observability
description: Audit embedded C++ rule 14 on the King board firmware: the board explains itself. Use when checking the architecture or before a release; read-only.
---

# Rule 14: the board explains itself

**The rule.** The firmware reports what it is (version, git hash, build time), why it last rebooted, what it is doing (states), what is wrong (faults), and its resource margins (heap, stack, tick time), with logs reachable without a cable.

**Why it matters.** A board that can't tell you why it misbehaved can only be debugged on the bench, which is useless once it is in someone's kitchen.

## What to check in this repo

- Version: `src/version.h`, shown on boot, in `/version`, `/status` and the console `?`.
- Reset reason: `resetReason()` logged at boot (`king_board.ino`).
- Health words: `/health`, `/status` and the console (`src/logic/health.h`, `src/net/report.*`).
- Margins: heap free and largest block, tick max and overruns. Check whether stack high-water marks are reported (`uxTaskGetStackHighWaterMark`).
- Logs: the ring buffer at `/log`, telnet mirror, and log lines never lost silently (queue size in `src/core/log.cpp`). Every line goes through `src/core/log.*`: `grep -rn 'Serial.print' firmware/king_board/src firmware/king_board/king_board.ino` finds only `core/log.cpp`, the vendored `sscma/`, and lines marked `// serial-only:`. Failures use `warnf`/`errorf`, counted in `/health` (`warnings`, `errors`).

## Verdict

PASS: version, reset reason, health, all margins and remote logs. PARTIAL: one margin missing, such as the stack. FAIL: only serial prints.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

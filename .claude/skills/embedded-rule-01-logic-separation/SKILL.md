---
name: embedded-rule-01-logic-separation
description: Audit embedded C++ rule 1 on the King board firmware: logic is separated from hardware. Use when checking the architecture or before a release; read-only.
---

# Rule 1: logic is separated from hardware

**The rule.** Decisions (what to do) live in pure C++ that never touches hardware. Hardware is reached through interfaces, so the same logic runs on a PC in tests.

**Why it matters.** Logic tied to pins and libraries can only be tested on the board, so every bug costs a flash cycle, and a bug that moves a motor can hurt someone.

## What to check in this repo

- `src/logic/` must not include `Arduino.h`, FreeRTOS, `Wire`, `WiFi` or any driver header. Check with `grep -rn '#include' firmware/king_board/src/logic`.
- Build `src/logic` on the host: `g++ -std=c++17 -fsyntax-only -I firmware/king_board/src firmware/king_board/src/logic/*.cpp`.
- Check whether drivers have an interface (abstract class or function table) that a test could replace. Free functions called directly from `src/app/control.cpp` count as no interface.
- Check whether `src/app/control.cpp` can be compiled and tested on the host with fake drivers.

## Verdict

PASS: logic is pure, and the control layer reaches drivers through replaceable interfaces with host tests. PARTIAL: logic is pure, but the control layer calls drivers directly. FAIL: logic includes hardware headers.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

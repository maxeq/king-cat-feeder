---
name: embedded-rule-13-testing
description: Audit embedded C++ rule 13 on the King board firmware: logic is tested on a PC and the board is tested on the bench. Use when checking the architecture or before a release; read-only.
---

# Rule 13: logic is tested on a PC and the board is tested on the bench

**The rule.** Pure logic has host unit tests, including randomized tests with safety invariants. The control glue is testable with fake drivers. A hardware-in-the-loop suite checks the real board. CI runs everything that doesn't need hardware.

**Why it matters.** Untested embedded code is tested by the cat.

## What to check in this repo

- Run the host tests: `cd firmware/king_board && g++ -std=c++17 -O2 -Wall -Wextra -I src test/test_logic.cpp src/logic/*.cpp -o /tmp/t && /tmp/t`. Report pass/fail and the check count.
- Read `test/test_logic.cpp`: are the invariants strong? Look for assertions that accept any result and for scenarios the fuzz never generates.
- Coverage gaps: `src/app/control.cpp` and the drivers have no host tests unless fakes exist.
- `test/hil/`: what does it cover, and is it runnable (`KING_HOST=... pytest firmware/king_board/test/hil`)?
- `.github/workflows/ci.yml`: which tests run in CI?

## Verdict

PASS: logic and control glue have host tests, HIL exists, CI runs them. PARTIAL: only logic is host-tested. FAIL: no automated tests.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

---
name: embedded-rule-15-cpp-subset
description: Audit embedded C++ rule 15 on the King board firmware: a safe C++ subset, enforced by tools. Use when checking the architecture or before a release; read-only.
---

# Rule 15: a safe C++ subset, enforced by tools

**The rule.** Embedded C++ avoids exceptions and RTTI, uses fixed-width integer types for anything with a range, keeps types consistent (one type for milliseconds), marks what doesn't change `const`, avoids implicit narrowing, and runs static analysis in CI.

**Why it matters.** These rules remove whole classes of bugs before the board runs.

## What to check in this repo

- Build with all warnings: `cd firmware/king_board && cp secrets.h.example secrets.h && arduino-cli compile --profile king_board --warnings all . ; rm secrets.h`. Count warnings in this repo's files (ignore the core and libraries).
- Time types: find `unsigned long`, `uint32_t` and `int` used for milliseconds. Report mixes.
- Find `throw`, `try`, `dynamic_cast` and `typeid`; they should not appear.
- `const` correctness of getters and parameters, and `static` for file-local state.
- Check whether clang-tidy or cppcheck runs in `.github/workflows/ci.yml`. If either is installed locally, run it on `firmware/king_board/src` (`cppcheck --enable=warning,style,performance --std=c++17 firmware/king_board/src`).
- Formatting is enforced: `.clang-format` plus the CI step.

## Verdict

PASS: zero warnings, consistent types, static analysis in CI. PARTIAL: zero warnings but no analyser, or mixed types. FAIL: warnings in our code.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

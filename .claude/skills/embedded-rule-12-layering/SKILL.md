---
name: embedded-rule-12-layering
description: Audit embedded C++ rule 12 on the King board firmware: dependencies point one way. Use when checking the architecture or before a release; read-only.
---

# Rule 12: dependencies point one way

**The rule.** Layers depend only downward (net → app → logic and drivers → core), with no cycles and no layer reaching up. Cross-layer callbacks go through registered hooks, not includes.

**Why it matters.** Upward dependencies turn the code into one ball where any change breaks anything.

## What to check in this repo

- For each folder in `firmware/king_board/src`, list its includes: `grep -rn '#include "\.\./' firmware/king_board/src/<layer>`.
- The allowed order, top to bottom: net → app → logic, drivers → core.
  - `logic` and `drivers` are siblings: neither may include the other.
  - `logic` may include only its own folder and the shared headers at the root of `src/` (`config.h`, `version.h`, `clock.h`, `types.h`). Every layer may include those.
  - Report every include that points up, or sideways between siblings, for example a driver including net or logic.
- `king_board.ino` may include everything; it is the composition root.
- Check for duplicate definitions of the same concept in two layers.

## Verdict

- PASS: no upward or sideways includes.
- PARTIAL: one shared header in the wrong place, used sideways (however many files include it), with no cycle.
- FAIL: any upward include, a cycle, or more than one misplaced header.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

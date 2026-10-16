---
name: embedded-audit
description: Run all 17 embedded C++ architecture rule checks on the King board firmware in parallel and summarise them in one table. Use for a full architecture review or before a release; read-only.
---

# Embedded architecture audit

Check the firmware in `firmware/king_board` against the 17 rules below. Each rule has its own skill in `.claude/skills/<name>/SKILL.md` and can be run alone, for example with `/embedded-rule-03-non-blocking`.

| # | Skill | Rule |
|---|---|---|
| 1 | `embedded-rule-01-logic-separation` | logic is separated from hardware |
| 2 | `embedded-rule-02-single-owner` | every resource has exactly one owner |
| 3 | `embedded-rule-03-non-blocking` | nothing blocks the control path |
| 4 | `embedded-rule-04-deterministic-timing` | timing is deterministic and measured |
| 5 | `embedded-rule-05-static-memory` | no heap churn in steady state |
| 6 | `embedded-rule-06-timeouts` | every wait has a timeout |
| 7 | `embedded-rule-07-watchdog-safe-state` | watchdog on every task, and a defined safe state |
| 8 | `embedded-rule-08-fault-handling` | faults are explicit, and escalation needs evidence |
| 9 | `embedded-rule-09-time-wraparound` | time arithmetic survives millis() wraparound |
| 10 | `embedded-rule-10-central-config` | configuration in one place, no magic numbers |
| 11 | `embedded-rule-11-persistent-state` | critical state survives a reboot |
| 12 | `embedded-rule-12-layering` | dependencies point one way |
| 13 | `embedded-rule-13-testing` | logic is tested on a PC and the board is tested on the bench |
| 14 | `embedded-rule-14-observability` | the board explains itself |
| 15 | `embedded-rule-15-cpp-subset` | a safe C++ subset, enforced by tools |
| 16 | `embedded-rule-16-security` | the device is secure for where it is deployed |
| 17 | `embedded-rule-17-ports-adapters` | the core talks to the world through narrow ports |

## How to run

1. Start one subagent per rule, all in the same message so they run in parallel. Give each this prompt: "Read `.claude/skills/<name>/SKILL.md` in this repo and do exactly what it says. Read-only." If the user names specific rules ("audit 3, 5 and 9"), run only those.
2. Wait for every report. Don't predict results.
3. Spot-check any FAIL against the code yourself before reporting it.

## Report

Read-only: no edits, commits or pushes.

Give one table with a row per rule: the number, the rule, the verdict (PASS, PARTIAL or FAIL) and the main reason in a few words.
After the table, list the top 5 fixes ordered by risk, each with `file:line` and one sentence.
End with the count: "N pass, N partial, N fail".

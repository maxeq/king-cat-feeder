---
name: embedded-rule-16-security
description: Audit embedded C++ rule 16 on the King board firmware: the device is secure for where it is deployed. Use when checking the architecture or before a release; read-only.
---

# Rule 16: the device is secure for where it is deployed

**The rule.** Firmware updates are authenticated, ideally with secure boot and signed OTA. Every remote interface needs credentials. Secrets are not in the repo, and the network attack surface is documented.

**Why it matters.** A feeder on home Wi-Fi with an open OTA port can be reflashed by anyone on that network.

## What to check in this repo

- Secrets: `secrets.h` is git-ignored, `secrets.h.example` has placeholders, and nothing real is in history (`git log -p | grep -i pass` style checks).
- Web: basic auth on every route (`route()` in `src/net/web.cpp`) and over plain HTTP. Check whether TLS is used.
- OTA: is a password required or optional (`SECRET_OTA_PASS`)? Is the image signed? Check whether secure boot or flash encryption is enabled in the build (sdkconfig of the core).
- Telnet: authentication?
- Is the README Security section accurate and complete?
- Which of these is prototype-acceptable and which must change before a product ships?

## Verdict

PASS: authenticated OTA (signed), auth on every interface, documented. PARTIAL: LAN-only with some auth and an accurate README. FAIL: open interfaces and undocumented.

## Report

Read-only: never edit, commit or push. Reply in this shape, under 300 words:

1. **Verdict:** PASS, PARTIAL or FAIL, plus one sentence why.
2. **Evidence:** what you checked, each item with `file:line`.
3. **Violations:** each one with `file:line`, what breaks and when, and the fix in one or two sentences.
4. **Next step:** the single change that moves this rule furthest.

Verify every claim against the code. Quote what you found; never infer it from names.
Paths are relative to the repo root. The firmware is in `firmware/king_board`.

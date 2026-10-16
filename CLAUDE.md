# CLAUDE.md

King Cat feeder firmware: an ESP32-S3 board that doses kibble by weight, opens a door for one recognised cat,
and talks to a Grove Vision AI V2 camera over I2C. `ml/` holds the recognition experiments.
The board design and mechanics are private and not in this repo.

The owner speaks Russian and is a TypeScript developer learning embedded: explain C++ and hardware choices
with TypeScript analogies. Code, comments and docs are in English.

## Setup and everyday commands

```sh
scripts/setup-dev.sh        # once: pinned clang-format, ruff, pytest in .venv-tools + git pre-commit hook
scripts/check.sh            # everything CI checks, except the firmware build (~5 s)
scripts/check.sh --build    # the same plus the firmware build

cd firmware/king_board
cp secrets.h.example secrets.h          # git-ignored; Wi-Fi, web and OTA passwords
arduino-cli compile --profile king_board .
arduino-cli upload  --profile king_board -p <port> .            # USB
arduino-cli upload  --profile king_board -p <board-ip> --upload-field password="" .   # OTA

g++ -std=c++17 -O2 -Wall -Wextra -I src test/test_logic.cpp src/logic/*.cpp -o /tmp/t && /tmp/t   # logic tests
KING_HOST=<board-ip> KING_PASS=<web password> pytest test/hil [--actuators]                    # live board
python3 web/build.py        # after editing web/*.html: regenerates src/net/pages.h
```

`sketch.yaml` pins the board (ESP32-S3-WROOM-1-N8R2: 8 MB flash, 2 MB quad PSRAM), the core (esp32 3.3.10)and every library. Always build with `--profile king_board`, never with a hand-typed FQBN. OTA keeps the old partition table: a board flashed with the default 4 MB layout needs one USB flash.
Before flashing over USB, cut the servo power, or the servos twitch while the PC drives the board.

## Reaching the board

After Wi-Fi connects, the board prints its address on serial: `>>> web: http://<ip>/`.
It also answers to `kingboard.local` through mDNS, the same name OTA uses. The console `?` command prints the IP too.

| What | Where | Login |
|---|---|---|
| Web UI | `http://kingboard.local/` or `http://<ip>/`, port 80 | basic auth from `secrets.h` (`SECRET_WEB_USER` / `SECRET_WEB_PASS`) |
| Telnet console | `nc kingboard.local 23` | none (LAN only) |
| OTA | `arduino-cli upload ... -p <ip>` | `SECRET_OTA_PASS`, empty = none |
| Serial | USB, 115200 baud | none |

Web routes:

| Route | What it serves |
|---|---|
| `/` | status and live log |
| `/status` | status as text, including both health words |
| `/health` | the health words, tick timing and uptime as JSON |
| `/log` | the log ring |
| `/cam` | one camera frame |
| `/frame?since=<at>` | camera JSON for the cats page; answers `{"same":true}` if nothing is newer |
| `/cats` | the cats-page mockup, with a live camera tab and the brightness slider |
| `/light?m=on\|pir` | light always on, or on motion |
| `/bright?v=0..100` | brightness |
| `/version` | version, git hash and build time |

Web and telnet start on the first Wi-Fi connection, even when that comes after boot.

## Architecture rules (keep them)

The layers depend only downward: `net` → `app` → `logic` and `drivers` → `core`.
`logic` and `drivers` are siblings and never include each other. Every layer may use the shared headers in the `src/` root:
`config.h`, `version.h`, `clock.h`.

| Layer                 | Rule                                                                                                                                                                                       |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `src/logic`           | Pure C++: no Arduino, FreeRTOS or driver headers. It gets inputs and returns commands, so it runs in `test/` on a PC. Fixed-size buffers only (`Messages`).                                |
| `src/app/control.cpp` | The control task, a 10 ms tick on core 1 at priority 5. It is the only code that touches actuators and sensors.                                                                            |
| `src/drivers`         | Non-blocking: commands set targets, `xxxTick(now)` advances. No `delay()` or unbounded wait reachable from a tick. Safety limits, such as the 20 s rotor cap, live here, not in the logic. |
| `src/net`             | Runs in `loop()` on core 1 at priority 1. Never touches hardware: it calls `controlSend(Cmd, arg)` and reads `controlStatus()`.                                                            |
| `src/core`            | Log (`say()` is safe from any task), NVS store, watchdog, workers, locks, OTA rollback.                                                                                                    |

- **One owner per resource.** The camera bus belongs to the `i2c` worker (`src/core/worker.h`) on core 0. A new slow device gets its own `Worker`; don't share one.
- **Every task subscribes to the watchdog and feeds it** on every path, including long loops. OTA unsubscribes during the write.
- **Time:** durations as `now - start`, deadlines with `due(now, t)` from `src/clock.h`. A deadline that has fired is cleared to 0. Set deadlines as `(now + ms) | 1` when 0 means "none".
- **Tuning values go in `src/config.h`.** Protocol constants stay named in their driver.
- **Health words:** faults and states are 16-bit words in `src/logic/health.h`. Bits are append-only: never renumber one. A new fault gets a bit, a name in `health.cpp`, a row in the README table, and a source in `faultWord()`.
- **NVS writes** come from the control task only, and never every tick (`storePut` skips unchanged values).
- **Web pages:** edit `firmware/king_board/web/*.html`, then run `web/build.py`. Never edit `src/net/pages.h`; a hook blocks it.

## Hardware facts that bite

- **Door servo (MF90)** is mounted upside down, so its scale is mirrored: closed = 175, open = 75. It holds its own angle; detach it when closed. There is no spare: be careful with stall tests.
- **Rotor servo (FS90R, continuous).** Drive its pin LOW on the first line of `setup()`. Detach it and hold the line LOW when idle, or it creeps. Its zero is set with the trimmer, in the middle of the dead band.
- **HX711.** Use 3.3 V only. `begin()` with a reset and a bare `read()` or `tare()` hang forever without a chip, so probe DT first and use `wait_ready_timeout`. Calibration is 363.7 with a positive sign. The tray must be empty at boot; a loaded tray keeps the saved tare.
- **LEDC.** The servos use ESP32Servo timer bookkeeping (`allocateTimer(1)`); the light is channel 4 at 5 kHz.
- **The PIR (HC-SR501)** is a wake-up signal, not a cat detector, and rotor vibration trips it. It is ignored while dosing.
- **Generic ESP32-S3 boards:** `CDCOnBoot=cdc` means USB serial is on. On the XIAO, the same menu means the opposite.

## Toolchain gotchas

- `Arduino.h` defines macros `bit()`, `LINE_MAX` and others, and POSIX has `alarm()`. Don't use these names.
- `std::mutex` adds ~50 KB of pthread code. Use `Mutex` / `Locked` from `src/core/lock.h`.
- macOS `sed` has no `\b`. Use Python for word-boundary replacements.
- clang-format runs after every edit (hook), so re-read a file before an exact-match edit of code you changed earlier.

## Working rules

- **Public repo:** never commit secrets, real Wi-Fi names, home IPs, personal data, photos, or CAD/PCB files. `secrets.h` and `web/demo_photos.json` are git-ignored.
- **Before saying "done":** run `scripts/check.sh` (the pre-commit hook does too). Run `--build` after firmware changes.
- **Logic changes come with tests** in `test/test_logic.cpp`: a named scenario, plus a fuzz case if it's a new kind of input.
- **Architecture review:** `/embedded-audit` runs all 16 rule checks in parallel; `/embedded-rule-NN-...` runs one (`.claude/skills/`).
- **Agents** (`.claude/agents/`, read-only):

  | Agent | When to use it |
  |---|---|
  | `firmware-reviewer` | after a change to dosing, the FSM, drivers or `control.cpp`, before committing |
  | `embedded-platform-reviewer` | after a change to tasks, drivers, net or core, or before flashing a new build |
  | `fix-verifier` | after fixing reported bugs: pass it the list, and it reproduces each scenario |
  | `bench-tester` | after flashing the bench board: runs the HIL suite, with `--actuators` only on request |

  Run the two reviewers in parallel. Send their findings to `fix-verifier` once fixed.
- **Versioning:** SemVer in `src/version.h`, notes in `CHANGELOG.md` under Unreleased, release by tag (see README).
- **Priorities:** architecture first. Don't start review-and-fix loops on logic edge cases unless asked; tune them on the bench.
- **Not run on hardware yet:** this event-driven version is verified by host tests and the build only. Say so when it matters.

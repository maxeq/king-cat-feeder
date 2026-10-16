# King Cat feeder

[![ci](https://github.com/maxeq/king-cat-feeder/actions/workflows/ci.yml/badge.svg)](https://github.com/maxeq/king-cat-feeder/actions/workflows/ci.yml)

Firmware for a cat feeder that is meant to recognise one cat by its face, without a collar or RFID tag:
only that cat gets the door opened, the kibble is dosed by weight, and the scale tracks what was eaten.

**Status: bench prototype.** The hardware is built, and every part worked on it with the earlier
single-file version of this firmware: weighed dosing, door, scale, light, PIR, web UI and OTA.
This restructured version is verified by the host tests and the build, but has not run on the board yet.
Recognition is not wired into the feeding yet.
The firmware talks to the Grove module and runs inference on whatever model is loaded; the cat-specific model is the next milestone.

```
Grove Vision AI V2 + OV5647   camera and NPU
          │ I2C
King board (ESP32-S3)         Wi-Fi, OTA, web UI, telnet
  ├─ rotor servo (FS90R)      doses kibble
  ├─ door servo  (MF90)       opens the bowl
  ├─ load cell + HX711        grams dosed and eaten
  └─ PIR, light, button, status LED
```

This repository holds the firmware and the recognition experiments.
The custom board and the mechanical design are not published.

## Architecture

```
firmware/king_board/
├── king_board.ino      boots the layers, starts the tasks; loop() is the network task
├── sketch.yaml         pinned board, core and library versions
├── secrets.h.example   Wi-Fi and password template
├── web/                web UI sources (HTML) and the bundler
├── test/               host tests of the feeder logic; hil/ = tests against a live board
└── src/
    ├── config.h        pins and tuning constants
    ├── version.h       firmware version
    ├── logic/          feeder state machine + dose controller: pure C++, no Arduino
    ├── app/            the control task that runs the logic against the hardware
    ├── drivers/        non-blocking hardware drivers
    ├── net/            Wi-Fi, web + OTA, telnet console
    └── core/           logging, NVS store, watchdog, OTA rollback, workers, locks
ml/                     recognition experiments (Python) and their tests
```

The firmware is event-driven, with no `delay()` in anything that moves.

| Task | Core, priority | Owns |
|---|---|---|
| `control` | 1, high | rotor, door, scale, light, button, PIR, indicator, the feeder state machine; ticks every 10 ms |
| `loop` | 1, low | web UI, OTA, telnet, log output |
| `i2c` | 0, low | the Grove camera: one exchange takes seconds |

- **Logic is separate from hardware.** `src/logic` holds the feeder state machine and the dose controller in plain C++. Each control tick they get a snapshot of the inputs and return commands, so they run unchanged in `test/` on a PC with a simulated rotor and scale.
- **One owner per resource.** Only the control task touches actuators and sensors. Only the `i2c` worker touches the camera bus. The web and the console send commands to the control task and read a status snapshot, and they never touch hardware.
- **Safety doesn't depend on the logic.**
  - Drivers cap a rotor left running at 20 s, latched until an operator stop.
  - Every task has a watchdog subscription, and the last reset reason is logged on boot.
  - A scale that stops answering stops the dose instead of escalating it.
  - Before an OTA write, everything goes to a safe state.
- **A bad update rolls itself back.** Three boots in a row that end in a crash or a watchdog reset, without a healthy minute in between, switch the board back to the previous firmware slot.
- **State survives a reboot.** The door angle, tare and light level live in NVS. After a power cut the door servo attaches at its real angle instead of slamming, and a boot with food or the cat on the tray keeps the saved tare.

### Health words

Two 16-bit words describe the board, the way a chip's status registers do. One number each is easy to log, show and send as telemetry.
They are shown in `/status`, as JSON at `/health` and in the console `?`.
The definitions are in `src/logic/health.h`. Bits are append-only, so a bit is never renumbered.

| Bit | Fault | Critical |
|---|---|---|
| 0 | `ScaleMissing`: no HX711 found since boot | |
| 1 | `ScaleLost`: the HX711 stopped answering | yes |
| 2 | `RotorTripped`: the 20 s safety stop is latched | yes |
| 3 | `GroveOffline`: the camera module doesn't answer | |
| 4 | `NoFood`: the last dose jammed, ran dry or timed out | yes |
| 5 | `DoseScaleFault`: the last dose stopped because the scale went quiet | yes |
| 6 | `WifiDown` | |
| 7 | `Halted`: safe state during OTA | |
| 8 | `BootCrashed`: the last reset was a crash or a watchdog | |
| 9 | `TickOverrun`: a control tick overran its 10 ms slot in the last 10 s | yes |

The indicator blinks while any critical bit is set.
The state word holds `Dosing`, `DoorOpen`, `DoorMoving`, `ScaleTaring`, `CatOnTray`, `AutoMode`, `LightOn`, `Motion` and `CameraBusy`.
`/health` also reports the slowest control tick since boot and how many ticks overran.

## Development

```sh
scripts/setup-dev.sh        # once: pinned clang-format, ruff and pytest in .venv-tools, plus the git pre-commit hook
scripts/check.sh            # what CI checks, in ~5 s
scripts/check.sh --build    # plus the firmware build
```

The pre-commit hook runs `scripts/check.sh` and refuses to commit `secrets.h`, photos, CAD or PCB files.
For [Claude Code](https://claude.com/claude-code), `CLAUDE.md` holds the project rules.
`.claude/settings.json` adds hooks:
- C++ is formatted after every edit;
- editing a web page rebuilds the bundle;
- generated and secret files can't be edited;
- a commit is checked first.

`.claude/agents/` defines reviewers for firmware behaviour and for the ESP32 platform, plus a fix verifier and a bench tester. `.claude/skills/` holds the architecture audits.

Flashing, tagging and pushing always ask for confirmation.

## Build and flash

```sh
cd firmware/king_board
cp secrets.h.example secrets.h      # Wi-Fi, web and OTA passwords

# the profile in sketch.yaml installs the exact core and libraries on first run
arduino-cli compile --profile king_board \
  --build-property "compiler.cpp.extra_flags=-DFW_GIT=\"$(git rev-parse --short HEAD)\""
arduino-cli upload --profile king_board -p <port>

# later, over Wi-Fi (password = SECRET_OTA_PASS, empty if unset)
arduino-cli upload --profile king_board -p <board-ip> --upload-field password=""
```

The profile builds for the ESP32-S3-WROOM-1-N8R2: 8 MB flash, 2 MB PSRAM, two 3 MB app slots.
OTA does not rewrite the partition table, so flash over USB once to switch a board from the default 4 MB layout.

## Tests

Three levels, from fastest to closest to the real thing:

| Level | What it checks | Where it runs |
|---|---|---|
| Logic tests | the dose controller and the feeder state machine in named scenarios: jam, cancel, timeout, scale fault, a cat stepping on, auto mode | on a PC, in CI |
| Randomized doses | 10 000 doses with random hoppers, jams, cats, scale dropouts and cancels. Invariants: the rotor always stops, a dose never outlives its timeout, the door opens at most once, "reached" is never the cat's weight | on a PC, in CI |
| Hardware-in-the-loop | a real board over Wi-Fi: version, login, `/status` speed, the camera never blocking the web, console, heap; with `--actuators` also the door, light and rotor | on the bench, against a live board |

```sh
cd firmware/king_board
g++ -std=c++17 -O2 -Wall -Wextra -I src test/test_logic.cpp src/logic/*.cpp -o test_logic && ./test_logic

pip install pytest
KING_HOST=<board-ip> KING_PASS=<web password> pytest test/hil              # read-only
KING_HOST=<board-ip> KING_PASS=<web password> pytest test/hil --actuators  # moves things
```

## Using it

- **Web UI** at `http://<board-ip>/`. It shows live status and the log, plus a single-frame camera page at `/cam`.
  `/cats` has a live camera tab and the light-brightness slider, next to a mockup of the planned cat profiles with sample data.
- **Telnet** on port 23 takes one-letter commands to drive the rotor, door, light, scale and Grove by hand.
  The full list is at the top of `king_board.ino`.
- **Button** with the door closed runs a dose. The rotor loosens the kibble and the door opens.
  Then the rotor feeds until the tray gains 10 g, escalating through anti-jam moves if nothing comes out.
  Without a scale it falls back to a timed dose. A second press cancels. With the door open, the button closes it.
  A dose can only be started with the button for now. The algorithm is in `src/logic/dose.cpp`.
  Something stepping on the tray is told apart from food by how the weight moves:
  - Kibble trickles in, so a jump of more than 6 g between two readings is not food.
  - The rotor is stopped during a weigh, so a reading that creeps up then is not food either.
  - Either way the rotor pauses. Once the weight starts to drop, which means the load is leaving, it weighs again and decides.
  - A dose only counts as done when two weighs in a row agree, because a paw shifts the weight and food doesn't.
  - A cat that never leaves ends the dose as "tray blocked", not as "no food".
  - A light paw lying perfectly still is the one case weight alone can't tell from food. That is for the camera to solve.
- **OTA hostname** is `kingboard`.

In this bench build the door opens as soon as dosing starts.
The state machine is in place for dosing behind a closed door and opening it for the recognised cat; the recognition event isn't wired in yet.

| Pin | GPIO |
|---|---|
| rotor servo | 4 |
| door servo | 5 |
| HX711 DT / SCK | 6 / 7 |
| light (PWM) | 8 |
| status LED | 9 |
| button | 10 |
| PIR | 11 |
| Grove SDA / SCL | 17 / 18 |
| spare | 21 |

## Web UI

The pages are plain HTML in `firmware/king_board/web/`.
`web/build.py` bundles them into `src/net/pages.h`, which the firmware serves; CI fails if the two are out of sync.
The index and cats pages are gzipped. `cam.html` is kept as plain text, because the firmware fills it in at runtime.
The cats page loads a font from Google Fonts and falls back to system fonts without internet.

```sh
python3 firmware/king_board/web/build.py
```

The cats page uses placeholder avatars.
If you put a `web/demo_photos.json` next to it, the bundler uses those photos instead; that file is git-ignored.

## Security

This is a LAN device for a home network.

- The web UI uses HTTP basic auth over plain HTTP, with credentials from `secrets.h`.
- Telnet has no authentication.
- OTA is protected only if `SECRET_OTA_PASS` is set.

Don't expose the board to the internet.

## Architecture checks

`.claude/skills/` holds 16 audit skills for [Claude Code](https://claude.com/claude-code), one per embedded C++ architecture rule, plus `/embedded-audit`, which runs them all in parallel and sums up the result in one table.
Each skill names the rule, why it matters, what to check in this repo, and what counts as pass, partial or fail. They only read the code.

| # | Rule | Skill |
|---|---|---|
| 1 | Logic is separated from hardware | `/embedded-rule-01-logic-separation` |
| 2 | Every resource has one owner | `/embedded-rule-02-single-owner` |
| 3 | Nothing blocks the control path | `/embedded-rule-03-non-blocking` |
| 4 | Timing is deterministic and measured | `/embedded-rule-04-deterministic-timing` |
| 5 | No heap churn in steady state | `/embedded-rule-05-static-memory` |
| 6 | Every wait has a timeout | `/embedded-rule-06-timeouts` |
| 7 | A watchdog on every task, and a safe state | `/embedded-rule-07-watchdog-safe-state` |
| 8 | Faults are explicit, escalation needs evidence | `/embedded-rule-08-fault-handling` |
| 9 | Time survives the millis() wrap | `/embedded-rule-09-time-wraparound` |
| 10 | Configuration in one place | `/embedded-rule-10-central-config` |
| 11 | Critical state survives a reboot | `/embedded-rule-11-persistent-state` |
| 12 | Dependencies point one way | `/embedded-rule-12-layering` |
| 13 | Tested on a PC and on the bench | `/embedded-rule-13-testing` |
| 14 | The board explains itself | `/embedded-rule-14-observability` |
| 15 | A safe C++ subset, enforced by tools | `/embedded-rule-15-cpp-subset` |
| 16 | Secure for where it is deployed | `/embedded-rule-16-security` |

## Versioning and releases

The firmware follows [Semantic Versioning](https://semver.org/). The version lives in `src/version.h`.
The board reports the version and git hash on boot, in the console `?` command, in `/status` and at `/version`. The boot line and `/version` also show the build time.

To release:

1. Move the `Unreleased` notes in `CHANGELOG.md` under a new version.
2. Bump `FW_VERSION` in `src/version.h`.
3. Commit, then tag and push.

```sh
git tag -a v0.2.0 -m "v0.2.0"
git push origin main v0.2.0
```

CI runs on pushes to `main`, on pull requests and on tags. It checks formatting, the web bundle, the feeder logic tests, the Python lint and tests, and builds the firmware.
On a `v*` tag it also checks that the tag matches `FW_VERSION` and publishes a GitHub release with the `.bin`.
That `.bin` is built from `secrets.h.example`, so it proves the build but has no Wi-Fi credentials: flash your own build.

## Recognition

`ml/` tests whether a pretrained model can tell our cat from others before any on-device model is trained.
See `ml/README.md`.

## License

[PolyForm Noncommercial 1.0.0](LICENSE.md). You may use, study, change and share this code for any
noncommercial purpose: personal projects, learning, research, hobby builds.
Selling it, or using it in a product or service you sell, needs a separate commercial license from the author.
To ask about one, open an issue or reach out through the author's GitHub profile.

This makes the project source-available rather than open source in the OSI sense.

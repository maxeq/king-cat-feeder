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
├── build_opt.h         -DUPDATE_SIGN: OTA accepts only signed images
├── ota_pubkey.h        the public half of the OTA signing key (scripts/ota-key.sh)
├── test/               host tests: feeder logic, control glue on fake hardware; hil/ = a live board
└── src/
    ├── config.h        pins and tuning constants
    ├── clock.h         wrap-safe deadlines (due(), Deadline)
    ├── version.h       firmware version
    ├── logic/          feeder state machine + dose controller: pure C++, no Arduino
    ├── types.h         plain types several layers share
    ├── app/            the controller (pure C++, hardware behind small ports), its task and adapters
    ├── drivers/        non-blocking hardware drivers; sscma/ = the Grove library, patched (MIT)
    ├── net/            Wi-Fi, web + OTA, telnet console
    └── core/           logging, NVS store (own worker), watchdog, OTA rollback, workers, locks
ml/                     recognition experiments (Python) and their tests
```

The firmware is event-driven, with no `delay()` in anything that moves.

| Task | Core, priority | Owns |
|---|---|---|
| `control` | 1, high | rotor, door, scale, light, button, PIR, indicator, the feeder state machine; ticks every 10 ms |
| `loop` | 1, low | web UI, OTA, telnet, log output |
| `i2c` | 0, low | the Grove camera: one exchange takes seconds |
| `nvs` | 0, low | NVS writes: milliseconds each, so never in the control tick |

- **Logic is separate from hardware.** `src/logic` holds the feeder state machine and the dose controller in plain C++. Each control tick they get a snapshot of the inputs and return commands, so they run unchanged in `test/` on a PC with a simulated rotor and scale.
  The glue that applies commands and drives the hardware, `src/app/controller.cpp`, reaches every device through small ports (`src/app/ports.h`: rotor, door, scale, light and so on), so it runs on a PC against fakes too. `control.cpp` adds the FreeRTOS task, the queue, the status lock and the adapters that implement each port with its driver: ports and adapters, wired in one place.
- **One owner per resource.** Only the control task touches actuators and sensors. Only the `i2c` worker touches the camera bus, and only the `nvs` worker writes NVS. The web and the console send commands to the control task and read a status snapshot, and they never touch hardware. They may also post a request to the camera worker and read its latest result: a queue and a locked snapshot, like the control task's.
- **Nothing waits without a limit.** Locks time out (and count it), sockets are written without blocking against one deadline per response, and every bus and sensor wait has a timeout. The watchdog is the backstop, not the plan.
- **No heap in steady state.** The control tick never allocates; log lines and web replies use fixed buffers, and camera frames live in three PSRAM buffers allocated at boot.
- **Safety doesn't depend on the logic.**
  - Drivers cap a rotor left running at 20 s, latched until an operator stop.
  - Every task has a watchdog subscription, and the last reset reason is logged on boot.
  - A scale that stops answering stops the dose instead of escalating it.
  - Before an OTA write, everything goes to a safe state, and every command is dropped until the update ends.
- **A bad update rolls itself back.** Three boots in a row that end in a crash or a watchdog reset, without a healthy minute in between, switch the board back to the previous firmware slot.
- **State survives a reboot.** The door angle (saved every 10° while it moves), tare, light level and light mode live in NVS. After a power cut the door servo attaches at its real angle instead of slamming, and a boot with food or the cat on the tray keeps the saved tare. A dose cut short by a reboot is reported on the next boot. Auto mode always boots off, so a reboot never starts feeding on its own.

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
| 4 | `NoFood`: the last dose jammed or ran dry: nothing came out | yes |
| 5 | `DoseScaleFault`: the last dose stopped because the scale went quiet | yes |
| 6 | `WifiDown` | |
| 7 | `Halted`: safe state during OTA | |
| 8 | `BootCrashed`: the last reset was a crash or a watchdog | |
| 9 | `TickOverrun`: a control tick overran its 10 ms slot in the last 10 s | yes |
| 10 | `DoseTimeout`: the last dose timed out: food came, but too slowly | yes |

The indicator blinks while any critical bit is set.
The state word holds `Dosing`, `DoorOpen`, `DoorMoving`, `ScaleTaring`, `CatOnTray`, `AutoMode`, `LightOn`, `Motion` and `CameraBusy`.
`/health` also reports the resource margins: the slowest control tick since boot and how many ticks overran, free heap and its largest block, free PSRAM, each task's smallest free stack, log lines lost and lock timeouts, plus the uptime, the last reset reason, and how many warnings and errors the log has recorded.
Every log line goes through one module (`src/core/log.*`), so it reaches the web log and telnet, not only USB; failures are marked `! ` (warning) or `!! ` (error).

## Development

```sh
scripts/setup-dev.sh        # once: pinned clang-format, clang-tidy, ruff, pytest in .venv-tools + the pre-commit hook
scripts/check.sh            # what CI checks, in ~10 s
scripts/check.sh --build    # plus the firmware build
```

The pre-commit hook runs `scripts/check.sh` and refuses to commit `secrets.h`, photos, CAD or PCB files.
For [Claude Code](https://claude.com/claude-code), `CLAUDE.md` holds the project rules.
`.claude/settings.json` adds hooks:
- C++ is formatted after every edit;
- editing a web page rebuilds the bundle;
- generated and secret files can't be edited;
- a commit is checked first.

`.claude/agents/` defines reviewers for firmware behaviour and for the ESP32 platform, an architecture guard that checks a change for regressions against the audit rules, plus a fix verifier and a bench tester. `.claude/skills/` holds the architecture audits.

Flashing, tagging and pushing always ask for confirmation.

## Build and flash

```sh
cd firmware/king_board
cp secrets.h.example secrets.h      # Wi-Fi, web and OTA passwords (the OTA one is required)

# the profile in sketch.yaml installs the exact core and libraries on first run
arduino-cli compile --profile king_board \
  --build-property "compiler.cpp.extra_flags=-DFW_GIT=\"$(git rev-parse --short HEAD)\""
arduino-cli upload --profile king_board -p <port>

# later, over Wi-Fi: builds, signs and uploads (asks for nothing: password from secrets.h)
cd ../.. && scripts/ota.sh <board-ip>
```

OTA images must be signed. `scripts/ota-key.sh` creates the key pair once: the private key stays in
`~/.config/king-board/` (or `$KING_OTA_KEY`), outside the repo, and the public key goes into
`firmware/king_board/ota_pubkey.h`, which is compiled in. Back the private key up: a board only takes
an image signed with it, so without it new firmware goes on over USB. Forks run `scripts/ota-key.sh`
to make their own pair. A plain `arduino-cli upload` over the network sends an unsigned image, and the
board rejects it at the end of the upload and carries on with the old firmware.

The profile builds for the ESP32-S3-WROOM-1-N8R2: 8 MB flash, 2 MB PSRAM, two 3 MB app slots.
OTA does not rewrite the partition table, so flash over USB once to switch a board from the default 4 MB layout.

## Tests

Three levels, from fastest to closest to the real thing:

| Level | What it checks | Where it runs |
|---|---|---|
| Logic tests | the dose controller and the feeder state machine in named scenarios: jam, cancel, timeout, slow flow, scale fault, a cat stepping on, auto mode, both millis() wraps | on a PC, in CI |
| Randomized doses and FSM | 10 000 doses with random hoppers, jams, cats, scale dropouts and cancels, and 2 000 runs of random presses and motion. Invariants: the rotor always stops, a dose never outlives its timeout, the door opens at most once, "reached" is never the cat's weight, a dead scale is never "no food" | on a PC, in CI |
| Control tests | the control glue (`controller.cpp`) against fake hardware: commands, the OTA halt, console pulses, the fault word, boot restore, `Deadline` across both wraps; plus 500 runs of random commands and halts with "nothing moves while halted" | on a PC, in CI |
| Driver tests | servos (the 20 s rotor cap across both wraps, idle detach, door steps and saves, the safe state), the button debounce after weeks idle, and the scale (tare, a loaded tray at boot, 24-bit sign, never blocking without a chip, lost and recovered), the light ramp and the PIR warm-up and self-test. The drivers compile unchanged against fakes of the ESP32 core and libraries in `test/fakes/` (a link seam), each test in its own process | on a PC, in CI |
| Static analysis | clang-tidy (`firmware/king_board/.clang-tidy`) on everything that builds on a PC; the firmware build fails on any compiler warning in our code | on a PC, in CI |
| Hardware-in-the-loop | a real board over Wi-Fi: version, login, `/status` speed, the camera never blocking the web, console, heap; with `--actuators` also the door, light and rotor | on the bench, against a live board |

```sh
cd firmware/king_board
g++ -std=c++17 -O2 -Wall -Wextra -I src test/test_logic.cpp src/logic/*.cpp -o test_logic && ./test_logic
g++ -std=c++17 -O2 -Wall -Wextra -I src test/test_control.cpp src/app/controller.cpp src/logic/*.cpp \
  -o test_control && ./test_control
g++ -std=c++17 -O2 -Wall -Wextra -I test/fakes -I src test/test_drivers.cpp test/fakes/fake_board.cpp \
  src/drivers/{servos,button,scale,light,pir}.cpp -o test_drivers && ./test_drivers

pip install pytest
KING_HOST=<board-ip> KING_PASS=<web password> pytest test/hil              # read-only
KING_HOST=<board-ip> KING_PASS=<web password> pytest test/hil --actuators  # moves things
```

## Using it

- **Web UI** at `http://<board-ip>/`. It shows live status and the log, plus a single-frame camera page at `/cam`.
  `/cats` has a live camera tab and the light-brightness slider, next to a mockup of the planned cat profiles with sample data.
- **Telnet** on port 23 asks for the web password, then takes one-letter commands to drive the rotor, door, light, scale and Grove by hand.
  The full list is at the top of `king_board.ino`. Serial takes the same commands with no password.
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

This is a LAN device for a home network. Every way in needs a credential:

| Interface | Protection |
|---|---|
| Web UI, port 80 | HTTP basic auth (`SECRET_WEB_USER` / `SECRET_WEB_PASS`) on every route. State changes (`/light`, `/bright`) need POST plus an `X-King` header, so another web page can't trigger them with the browser's cached login. |
| Telnet, port 23 | the web password, three tries, 30 s to type it. It can drive the rotor and door. |
| OTA, port 3232 | `SECRET_OTA_PASS` (the build fails without one) **and** an ECDSA P-256 signature checked against `ota_pubkey.h` before the new image may boot. |
| mDNS | advertises `kingboard.local` and the OTA service on the LAN. |
| Serial (USB) | no password: it takes physical access. |

The board warns in the log at boot if a password is still the example one.

What it does not have, and why that is acceptable here:
- **No TLS.** Basic auth travels in clear text on the LAN, so anyone who can sniff the home network can read the web password. Don't reuse it elsewhere.
- **No secure boot or flash encryption.** The prebuilt Arduino core is built without them. Someone with the board in hand can read the flash (Wi-Fi password included) or flash anything over USB. Turning them on needs an ESP-IDF build and burns eFuses for good.

Don't expose the board to the internet. Before this becomes a product: TLS or a pairing scheme instead of basic auth, secure boot v2 with flash encryption, and no telnet.

## Architecture checks

`.claude/skills/` holds 17 audit skills for [Claude Code](https://claude.com/claude-code), one per embedded C++ architecture rule, plus `/embedded-audit`, which runs them all in parallel and sums up the result in one table.
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
| 17 | The core talks to the world through narrow ports | `/embedded-rule-17-ports-adapters` |

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

CI runs on pushes to `main`, on pull requests and on tags. It checks formatting, the web bundle, the logic and control tests, clang-tidy, the Python lint and tests, and builds the firmware with every warning on.
On a `v*` tag it also checks that the tag matches `FW_VERSION` and publishes a GitHub release with the `.bin`.
That `.bin` is built from `secrets.h.example`, so it proves the build but has no Wi-Fi credentials: flash your own build.
If the repository has an `OTA_SIGNING_KEY` secret (the private key PEM), CI also publishes a signed copy for OTA.

## Recognition

`ml/` tests whether a pretrained model can tell our cat from others before any on-device model is trained.
See `ml/README.md`.

## License

[PolyForm Noncommercial 1.0.0](LICENSE.md). You may use, study, change and share this code for any
noncommercial purpose: personal projects, learning, research, hobby builds.
Selling it, or using it in a product or service you sell, needs a separate commercial license from the author.
To ask about one, open an issue or reach out through the author's GitHub profile.

This makes the project source-available rather than open source in the OSI sense.

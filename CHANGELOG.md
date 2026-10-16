# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the firmware uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

Brings every rule of the architecture audit (`/embedded-audit`) to PASS. Not run on hardware yet.

### Changed
- **OTA needs a signed image and a password.** `ArduinoOTA` checks an ECDSA P-256 signature against `ota_pubkey.h`, and the build fails without `SECRET_OTA_PASS`. Upload with `scripts/ota.sh`; create the key once with `scripts/ota-key.sh`. A board running 0.1.0 accepts the first signed image over OTA.
- **Telnet asks for the web password.** The HIL suite sends it.
- `/light` and `/bright` change state only on POST with an `X-King: 1` header.
- The control decisions moved from `control.cpp` into `Controller` (`src/app/controller.cpp`), which reaches the hardware through ten small ports (`src/app/ports.h`: rotor, door, servos, scale, light, indicator, inputs, peers, store, log) and is tested on a PC against fakes (`test/test_control.cpp`). The adapters in `control.cpp` implement the ports with the drivers. Types several layers share moved to `src/types.h`.
- The network layer reaches the camera through `src/app/camera.h` and includes no driver.
- NVS writes go to a new `nvs` worker, so a flash write never stalls the control tick. The rollback counter is cleared from the control task.
- An OTA halt is a flag, not a queued command: a full queue can't lose it, and every command is dropped until the update ends. A dose running at the halt is aborted.
- Tuning values moved into `config.h`, every millisecond value is `uint32_t`, and drivers use a `Deadline` timer (`src/clock.h`).
- Locks time out after a second and count it; web and telnet replies are written without blocking against one deadline; a slow peer is dropped.
- Log lines, web replies and camera frames use fixed buffers (frames: three PSRAM buffers), so nothing allocates per tick or per request on the main paths.
- The Grove library (Seeed Arduino SSCMA 1.0.3, MIT) is now a patched copy in `src/drivers/sscma/`: replies go into buffers allocated once and a fixed JSON arena, and each read pass is capped so the library's own timeouts hold. Upstream spun forever when a reply's `malloc` failed, and wrote one byte past its receive buffer after a full read.
- The scale reads the HX711 with its own 24-bit read that never waits; the library's `read()` waited for DT without a limit.
- The door angle is saved every 10° while it moves; the light mode is restored at boot; a dose cut short by a reboot is reported on the next boot.

### Added
- Fault bit 10, `DoseTimeout`: a dose that timed out on slow flow is no longer reported as `NoFood`.
- One log: every line goes through `src/core/log.*` and reaches `/log` and telnet (OTA, rollback, scale and camera boot messages used to go to USB only). `warnf`/`errorf` mark and count problems; `/health` reports `warnings` and `errors`; check.sh fails on a stray `Serial.print`.
- `/health` reports free heap and its largest block, free PSRAM, each task's free stack, lost log lines, lock timeouts and the reset reason. `/status` shows the reset reason.
- clang-tidy in `scripts/check.sh` and CI; the CI firmware build fails on any warning in our code.
- Tests: drivers on the host through a link seam (`test/test_drivers.cpp`, fakes in `test/fakes/`): the rotor cap, door steps, safe state, button debounce after weeks idle, scale tare and recovery. Control glue, `Deadline` across both wraps, a randomized FSM run, slow flow, a scale that dies at the timeout, the 2^32 wrap for the FSM.

### Fixed
- A rotor started mid-tick could trip the 20 s safety stop at once: its start time was stamped after the tick's `now`, and the unsigned difference wrapped. The light ramp could skip for the same reason.
- `TickOverrun` came back for 10 s about 49.7 days after the last overrun.
- A scale that went quiet at the dose timeout was reported as "no food" instead of a scale fault.
- The Grove "available" count read its two bytes in an unspecified order.
- The motion light could stay on after auto mode was used for weeks: its off-deadline went stale while auto was on.
- The button stopped working after about 25 days without a press (found in review of this change, before release).
- An empty hopper that ran into the timeout could be reported as slow flow; a timeout with nothing dispensed is "no food".
- Inference is skipped when memory is too low for the camera library's reply buffer, which would otherwise spin forever.

## [0.1.0] - 2026-10-16

First public version. Runs on the King board bench prototype.

### Added
- Firmware for the King board (ESP32-S3), event-driven:
  - a pure C++ feeder state machine and dose controller in `src/logic`, with host-side tests on a simulated rotor and scale;
  - a 10 ms control task that owns every actuator and sensor;
  - non-blocking drivers;
  - a network task that only sends commands and reads a status snapshot;
  - a camera worker on the other core.
- Weighed dose on the button: feeds until the tray gains 10 g, escalates through anti-jam moves, cancels on a second press.
  - A weight step too fast for kibble, or a reading that creeps up while the rotor is stopped, pauses the rotor. Once the load starts to leave, a fresh weigh decides whether to feed on.
  - The target only counts after two agreeing weighs.
  - A cat that never leaves ends the dose as "tray blocked" without a no-food alarm.
- Door control, PIR-driven light and an optional PIR auto mode.
- Grove Vision AI V2 link over I2C: inference, snapshots, bus recovery and a scan tool. Camera views work during a dose and never block the web.
- Safety:
  - a task watchdog on every task, with the reset reason logged on boot;
  - a 20 s rotor cap in the driver, latched until an explicit stop;
  - a scale fault stops the dose without escalation;
  - a safe state before OTA writes.
- Health words: a 16-bit fault word and a 16-bit state word in `/status`, `/health` (JSON) and the console. The indicator blinks on any critical fault. Control-tick timing (slowest tick, overruns) is measured.
- App-level OTA rollback: three crashed boots in a row without a healthy minute switch back to the previous firmware slot.
- Tests:
  - logic tests on the host;
  - 10 000 randomized doses checking safety invariants;
  - a hardware-in-the-loop pytest suite for a live board.
- State in NVS: door angle (no servo slam after a power cut), tare (a loaded tray at boot keeps the saved zero), light level. A scale plugged in after boot is picked up and tared.
- Web UI with status, log and a camera page, plus a mockup of the cat-profiles page. Sources live in `web/` and are bundled by `web/build.py`.
- Web and OTA credentials in `secrets.h`. The web UI uses basic auth, and OTA takes an optional password.
- Web, OTA and telnet start on the first Wi-Fi connection, even when it happens after boot.
- Telnet console with one-letter test commands for every part.
- Firmware version and git hash on boot, in `?`, in `/status` and at `/version`, with build time on boot and at `/version`.
- Build for the real module (8 MB flash, PSRAM), pinned in `sketch.yaml`, with `clang-format` and `ruff` configs.
- CI covering formatting, the web bundle, the feeder logic tests, Python lint and tests, and the firmware build. On tags it publishes a release.
- PolyForm Noncommercial 1.0.0 license: free for noncommercial use, commercial use reserved.
- Recognition experiments in `ml/`: frame extraction, cat and face crops, DINOv2 embeddings and FAR/FRR evaluation. Enrollment and test can come from different videos. Includes tests on synthetic data.

### Known issues
- Dosing happens with the door open (bench behaviour). The recognition event that would open it only for the right cat isn't wired in yet.
- Not yet run on hardware: written and tested on the host and by build. Flash over USB once to get the 8 MB partition table.
- Telnet has no authentication. The device is meant for a home LAN only.

[Unreleased]: https://github.com/maxeq/king-cat-feeder/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/maxeq/king-cat-feeder/releases/tag/v0.1.0

# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the firmware uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

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

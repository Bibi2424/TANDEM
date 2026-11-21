# AGENTS.md — T.A.N.D.E.M.

**T.A.N.D.E.M.** (*Twin Axes, No Drift, Except Maybe*) is a DIY dual-joystick (HOSAS) controller project.

## Source of Truth

- **Requirements live in [docs/requirements.md](docs/requirements.md)** (overview in [README.md](README.md)). Do not duplicate product requirements in this file — `AGENTS.md` defines how to organize, build, test, and evolve the codebase, while `docs/requirements.md` captures target capabilities.
- **Keep this file current:** Update `AGENTS.md` in the same change whenever scaffolding, directory layout, `platformio.ini` environments, build/test commands, or repo conventions are introduced or changed. Do not document nonexistent paths or commands as if they already exist.

---

## Development Approach & Architectural Boundaries

1. **Explore Transport Modes Independently First**
   - Treat connectivity modes (e.g., wired USB HID, BLE HID, custom wireless link + base station, BLE configuration) as **independent prototypes/modules to explore and validate on their own** before deciding whether or how to unify them.
   - Avoid premature cross-mode state machines or heavy upfront unification unless explicitly requested.
   - When multiple wireless stacks are eventually combined on one MCU, keep radio lifecycle teardown/exclusivity explicit so stacks do not contend.

2. **Stay MCU-Portable (No Early Hardware Lock-In)**
   - Final MCU selection is still open (**ESP32-S3** is a leading candidate, **not yet a committed choice**). Initial prototyping starts on available **ESP32-S2** (native USB + Wi-Fi/ESP-NOW, no BLE) and **ESP32-WROOM** (BLE + Wi-Fi/ESP-NOW, no native USB) dev boards, with board-dependent feature subsets.
   - Keep MCU-, board-, and SDK-specific code (ADC, GPIO, USB, BLE, wireless radios, non-volatile storage, RTOS primitives) isolated behind thin adapters and board-config headers — never leak target-specific APIs or pin numbers into core logic.

3. **Keep Core Logic Pure & Decoupled**
   - Put hardware- and transport-independent logic (input calibration/deadzone/curve shaping, failsafe timeouts, config schema & CRC validation, binary packet codecs, report structures) in **standard, dependency-free C++**.
   - This lets prototypes share core behavior without duplicating math, makes switching MCUs cheap, and enables fast native host unit tests.

4. **Single Source of Truth for Shared Contracts**
   - Whenever prototypes or tools share wire packet layouts, HID report descriptors, or config/GATT schemas, define them once in shared headers/specs with explicit versioning and input validation.
   - Treat changes to established HID report descriptors or persisted config schemas as breaking changes requiring explicit documentation.

---

## Hardware & Portability Guardrails

- **Per-board pin configuration:** Keep pin assignments in target/board-specific headers and check each active MCU's datasheet for reserved pins (native USB, boot/strapping pins, flash/PSRAM, ADC/radio conflicts).
  - *ESP32-WROOM (classic ESP32):* BLE + Wi-Fi/ESP-NOW, **no native USB**; use **ADC1 (`GPIO32`–`GPIO39`, noting `34`–`39` are input-only)** while Wi-Fi/ESP-NOW is active (ADC2 conflicts with Wi-Fi), and avoid SPI flash pins (`GPIO6`–`GPIO11`) and strapping pins (`GPIO0`, `GPIO2`, `GPIO5`, `GPIO12`, `GPIO15`).
  - *ESP32-S2:* Native USB (`GPIO19`/`GPIO20`) + Wi-Fi/ESP-NOW, **no BLE**; prefer **ADC1 (`GPIO1`–`GPIO10`)** over ADC2 when Wi-Fi/ESP-NOW is active, and avoid strapping pins (`GPIO0`, `GPIO45`, `GPIO46`).
  - *ESP32-S3 (candidate):* Native USB (`GPIO19`/`GPIO20`) + Wi-Fi/ESP-NOW + BLE; prefer **ADC1 (`GPIO1`–`GPIO10`)** over ADC2 when Wi-Fi/ESP-NOW is active, and avoid strapping pins (`GPIO0`, `GPIO3`, `GPIO45`, `GPIO46`).
- **Power-path safety:** In any schematics, wiring docs, or hardware notes, require a proper LiPo/Li-ion charging and power-path/protection circuit — **never** wire battery voltage directly to 5V VBUS.
- **Analog & noise hygiene:** Keep ADC sampling deterministic and low-latency, and note analog/power-rail separation from RF and switching noise in hardware docs.

---

## Build Workflow & Modularity

- **Build tool:** PlatformIO (`pio` via `%USERPROFILE%\.platformio\penv\Scripts`), configured in [platformio.ini](platformio.ini).
- **Directory layout:**
  - `lib/tandem_core/` — Pure, dependency-free C++17 core logic (types, input pipeline, curve/deadzone math, failsafe logic, codecs, config validation), shared across all firmware targets and native tests.
  - `lib/tandem_hal/` — MCU/board-isolated ADC1 and GPIO input scanner adapter (`espressif32`-scoped) sampling raw axes, debounced buttons, and optional 4-way hat into `tandem::RawStickInput`.
  - `include/boards/` — Per-board capability flags and pinmaps (`board_config.h`, `esp32_wroom.h`, `esp32_s2.h`, `esp32_s3.h`), selected via `-DTANDEM_BOARD_*` build flags.
  - `src/apps/<name>/` — Standalone firmware apps and independent transport prototypes (`bringup`, `input_diag`, `usb_hid`), selected per environment via `build_src_filter = -<*> +<apps/<name>/>`.
  - `test/` — Native host Unity unit tests (`test/test_core/`, `test/test_axis_shaping/`, `test/test_input_pipeline/`, `test/test_hid_report/`).
- **Defined environments & commands:**
  - Native unit tests: `pio test -e native`
  - ESP32-WROOM bring-up (`esp32dev`): `pio run -e esp32_wroom_bringup`
  - ESP32-S2 bring-up (`lolin_s2_mini`; if USB CDC isn't active yet, enter ROM bootloader via `BOOT`/`GPIO0` + `RST`): `pio run -e esp32_s2_bringup`
  - Input diagnostics (ADC1 jitter & debounced GPIO): `pio run -e esp32_wroom_input_diag`, `pio run -e esp32_s2_input_diag`
  - Wired USB HID prototype (composite USB CDC + 1000 Hz USB HID Joystick): `pio run -e esp32_s2_usb_hid`
  - Serial console: `pio device monitor -b 115200` (add `-p <PORT>` if multiple boards are connected; on ESP32-S2 native USB CDC, press `RST` after attaching if you want to see the boot banner)
  - All default firmware targets: `pio run`
- **CI:** GitHub Actions ([.github/workflows/ci.yml](.github/workflows/ci.yml)) runs `pio test -e native -v` and `pio run` on pushes to `main` and pull requests.

---

## Embedded C++ Coding Guidelines

- **Real-time hot-path discipline:** Avoid dynamic heap allocation (`new`, `malloc`, growing containers, dynamic string churn) and blocking delays inside input sampling, radio TX/RX, or HID reporting loops.
- **Concurrency & ISR/callback safety:** Radio, USB, and BLE callbacks often run in interrupt or separate RTOS task contexts. Keep them short and non-blocking; hand data to the main loop via explicit synchronization (queues, mutexes, atomics, or double-buffered snapshots).
- **Explicit binary layouts:** Use fixed-width integer types (`uint8_t`, `int16_t`, `uint32_t`), explicit endianness/packing, versioning, and `static_assert` size checks for wire packets, HID reports, and persisted config structs.
- **Defensive math & timing:**
  - Guard against degenerate calibration bounds (`min >= center`, `center >= max`, zero spans), deadzones `>= 1.0`, `NaN`/`Inf`, and signed integer asymmetry (`-32768` vs `+32767`).
  - Handle unsigned timer wraparound via subtraction (`(uint32_t)(now - prev) >= interval`).
- **Compiler hygiene:** Fix compiler warnings rather than suppressing them; minimize global mutable state.

---

## Verification & Quality Gates

1. **Native Host Unit Tests (for Pure Logic)**
   - Unit test code whenever possible. Changes to input shaping (calibration, deadzone rescaling, curves), failsafe timeouts, packet codecs, config schema/CRC validation, or any other host-testable logic must include native host unit tests covering nominal behavior and edge cases (boundaries, sign preservation, degenerate bounds, corrupt CRC, version mismatch, timeouts).
2. **Affected-Target Builds**
   - Verify that all affected environments compile and pass cleanly (`pio test -e native` and `pio run` / affected `-e <env>` targets) before finishing.
3. **Hardware-in-the-Loop (HIL) Honesty & Pre-PR Gate**
   - Never claim on-hardware behavior (ADC jitter/centering, wireless reliability/drop rates, USB/BLE HID smoothness or polling rate, power-cycle persistence) is verified based solely on compilation or host unit tests. Clearly list remaining hardware checks for the active target board and mode.
   - Code involving modifications only testable on hardware **must be verified on physical hardware before a PR can be opened**, unless the human explicitly specifies otherwise.

---

## Git, Issues & Workflow Conventions

- **Human-controlled workflow phases:** Transitions between workflow phases are human-controlled. AI agents **must ask permission before committing, pushing, or opening a PR**.
- **Issue hierarchy:** Track work in GitHub Issues (`Bibi2424/TANDEM`) using native **Feature** issues as top-level parents, broken into PR-sized **Task** sub-issues only when ready to implement them (and **Bug** issues for defects).
- **Assignment:** Assign active issues (tasks being worked on, and their parent feature when active) to `Bibi2424` when starting work on them.
- **Branching:** Do not work or commit directly on `main`; create a topic branch (e.g., `feat/<name>`, `fix/<name>`, `docs/<name>`, `chore/<name>`) for all new changes.
- **Commits:** Use Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`) and reference relevant issue numbers (e.g., `(#9)`), matching existing repo history.
- **Pull Requests & closing issues:**
  - Open PRs against `main` for review and CI verification; do not merge PRs unless explicitly requested.
  - Use `Closes #<N>` for the specific **Task** or **Bug** issues completed by the PR.
  - Use `Part of #<N>` for ongoing umbrella **Feature** issues (such as DevOps `#8`) — only close a parent **Feature** when its full planned scope is actually complete.
- **Keep repo clean:** Never commit build outputs (`.pio/`, `build/`, `*.bin`, `*.elf`), IDE clutter, or secrets/credentials (`.env`, `*.key`, `*.pem`).
- **Sync docs with code:** Update `docs/`, protocol/schema specs, and this `AGENTS.md` whenever layouts, contracts, pinouts, or build commands change.

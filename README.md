# Tandem Controls

**T**win **A**xes, **N**o **D**rift, **E**xcept **M**aybe — **C**alibrated **O**nline, **N**o **T**rouble, **R**eally, **O**nly **L**ight **S**oldering

[![CI](https://github.com/tandem-controls/tandem/actions/workflows/ci.yml/badge.svg)](https://github.com/tandem-controls/tandem/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Build: PlatformIO](https://img.shields.io/badge/Build-PlatformIO-orange.svg)](platformio.ini)
[![Tandem Configurator](https://img.shields.io/badge/Tandem%20Configurator-Open%20Web%20App-2ea44f?logo=googlechrome&logoColor=white)](https://tandem-controls.github.io/tandem/)

An open-source, modular controller platform for **single-stick, HOTAS, HOSAS, and custom hybrid rigs** (button boxes, throttles, pedals), built around a portable C++17 core (`tandem_core`) so connectivity modes and hardware targets evolve cleanly without rewriting input math.

---

## Why Tandem Controls?

- **Zero-Install Browser Tooling ([Tandem Configurator](https://tandem-controls.github.io/tandem/)):** Calibrate axes, tune center/edge deadzones and response curves, inspect live ADC noise/circularity, and run `joy.cpl`-style Gamepad API tests directly in Chrome/Edge over **WebHID**, **WebSerial**, or **Web Bluetooth** (`tools/configurator/`).
- **Multi-Transport Connectivity:** Unified capability-gated firmware (`src/apps/stick/` and `src/apps/base_station/`) supporting wired USB HID (`Mode C`), direct BLE HID (`Mode A`), and low-latency ESP-NOW + USB Base Station (`Mode B`, up to `1000 Hz` USB HID) with strictly mutually exclusive wireless radio teardown.
- **Flexible Rig & Stick Mapping:** Supports standalone single-stick or custom controller setups as well as matched dual-stick HOSAS/HOTAS rigs (`Dual 2x` independent gamepads or `Combined 1x` USB HID via the Base Station).
- **MCU-Portable C++17 Core (`lib/tandem_core/`):** Hardware- and transport-independent Stage 1/Stage 2 input shaping, CRC-32-validated NVS persistence, binary wire codecs, and failsafe logic verified by native host unit tests (`pio test -e native`). Final MCU selection remains open (**ESP32-S3** is a leading candidate alongside active **ESP32-S2** and **ESP32-WROOM** prototyping targets).
- **Analog & Power-Path Hygiene:** Designed around clean ADC signal conditioning and safe LiPo/Li-ion charging + power-path protection (never wiring battery voltage directly to 5V `VBUS`), with open-source PCB schematics and enclosures planned on the path to `v1.0.0`.

### Transport Modes Overview

| Mode | Transport / Feature | Required Capabilities | Initial Prototyping Target |
| :--- | :--- | :--- | :--- |
| **Mode A** | Direct BLE HID gamepad to PC | BLE | `ESP32-WROOM` |
| **Mode B** | Low-latency ESP-NOW + USB Base Station *(up to 1000 Hz USB HID)* | Wi-Fi / ESP-NOW + Native USB *(base)* | `ESP32-WROOM` -> `ESP32-S2` |
| **Mode C** | Wired USB HID gamepad | Native USB | `ESP32-S2` |
| **Config** | WebHID, USB CDC (`WebSerial`), & BLE GATT (`Web Bluetooth`) + NVS persistence | USB / BLE + NVS | `ESP32-S2`, `ESP32-WROOM`, `ESP32-S3` |

---

## Repository Structure

```text
tandem/
├── .github/workflows/ci.yml           # GitHub Actions CI (format, native tests, firmware builds)
├── .github/workflows/pages.yml        # GitHub Pages deployment for tools/configurator/
├── .github/workflows/release.yml      # Tagged SemVer release builds (merged 0x0 binaries + checksums)
├── docs/quickstart.md                 # End-user flashing, pinouts, mode switching & configurator guide
├── docs/hil_release_checklist.md      # Release Hardware-in-the-Loop (HIL) smoke-test checklist
├── docs/requirements.md               # Baseline hardware, firmware, and verification requirements
├── include/boards/                    # Per-board capability flags and ADC1/GPIO pinmaps
├── lib/tandem_core/                   # Pure, dependency-free C++17 core library (host-testable)
├── lib/tandem_hal/                    # Board/MCU-isolated ADC1, GPIO, NVS & RTOS transport adapters
├── src/apps/                          # Firmware apps (stick, base_station) selected via build_src_filter
├── tools/configurator/                # Zero-build WebHID/WebSerial/Web Bluetooth app (Tandem Configurator)
├── test/                              # Native host Unity unit tests (core, shaping, codecs, failsafe)
├── platformio.ini                     # PlatformIO environments (native, ESP32-WROOM, ESP32-S2, ESP32-S3)
├── AGENTS.md                          # Repository development & coding conventions
└── LICENSE                            # MIT License
```

---

## Getting Started

> [!TIP]
> **New to Tandem Controls?** Start with the **[User Quick-Start Guide](docs/quickstart.md)** for prebuilt `0x0` flashing, board pinouts, `BOOT` button controls, mode switching, and calibration.
>
> **Tandem Configurator:** **[Open Live Configurator (`tandem-controls.github.io/tandem`)](https://tandem-controls.github.io/tandem/)** — Zero-install browser tool (Chrome / Edge) for live stick inspection, axis calibration, deadzones, response curves, and flash saving over **USB (`WebHID` / `WebSerial`)** or **BLE (`BLE_CFG` / Web Bluetooth)**.
> For **BLE**, switch the stick to `BLE_CFG` (`'4'` on serial or `BOOT` long-press) and click **Connect BLE** directly in Chrome/Edge — **do not** pair `TANDEM-CFG-...` in Windows Bluetooth Settings.
> Testing local edits? Run `python tools/configurator/serve.py` and open `http://localhost:8000/`.

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) (`pio`)
- A host C++17 compiler (`g++` / `clang++`) for running native unit tests

### Build & Test

```bash
# Run native host unit tests for tandem_core
pio test -e native

# Build all default firmware environments
pio run

# Build a specific board target
pio run -e esp32_wroom_stick
pio run -e esp32_s2_stick
pio run -e esp32_s2_base_station
pio run -e esp32_s3_stick
pio run -e esp32_s3_base_station

# Flash and open the serial console (115200 baud)
pio run -e esp32_s2_stick -t upload
pio device monitor -b 115200   # add -p <PORT> if multiple boards are connected

# Serve Tandem Configurator locally for Chrome/Edge testing (WebSerial & Web Bluetooth)
python tools/configurator/serve.py
```

### Local Port Overrides (`platformio_local.ini`)

`platformio.ini` includes an optional, gitignored `platformio_local.ini` via `extra_configs`. Create it in the repo root to pin machine-specific `upload_port` / `monitor_port` values per environment when multiple boards are connected:

```ini
[env:esp32_wroom_stick]
upload_port = COM7
monitor_port = COM7

[env:esp32_wroom_stick_right]
upload_port = COM12
monitor_port = COM12
```

### Editor / LSP Setup (`clangd`)

To get accurate C++ IntelliSense in editors using `clangd`:

1. **Generate `compile_commands.json` for the environment you are editing** (pass `-e <env>` so PlatformIO doesn't overwrite it with the last `default_envs` entry):
   ```bash
   pio run -t compiledb -e native
   # or for a specific ESP32 app/board, e.g.:
   pio run -t compiledb -e esp32_s2_stick
   ```
2. **Configure your `clangd` client with `--query-driver`** so it can resolve system headers (`<cstdint>`, etc.) from the PlatformIO/host compiler (unsupported Xtensa/GCC flags are already stripped by [.clangd](.clangd)):
   ```text
   --query-driver=**/*xtensa-esp32*-elf-*,**/*g++*
   ```

---

## Inspiration & Related Projects

Tandem Controls builds on ideas proven across open-source controller and input tooling:

- **[FreeJoy](https://github.com/FreeJoy-Team/FreeJoy)** — Deep STM32 flight-controller configurability, Hall/pot axis calibration, and custom flight-rig ergonomics.
- **[GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE)** — Clean multi-platform controller firmware structure and accessible browser-based configuration workflows.
- **[HID Remapper](https://github.com/jfedor2/hid-remapper)** — Portable expression/remapping concepts and WebHID-first zero-install configuration.
- **[ZMK](https://zmk.dev/) / [VIA](https://www.caniusevia.com/)** — Wireless-first input ergonomics, power-tier discipline, and instant live-tuning UI responsiveness.
- **[Joypad OS](https://github.com/joypad-ai/joypad-os)** — Web-configurator UX inspiration adapted for high-resolution multi-axis simulation diagnostics.

---

## Documentation & Roadmap

- [User Quick-Start Guide](docs/quickstart.md)
- [Release HIL Smoke-Test Checklist](docs/hil_release_checklist.md)
- [Technical Requirements](docs/requirements.md)
- [C++ Coding Standard](docs/coding_standard.md)
- [Development & Agent Guidelines](AGENTS.md)
- [Issue Tracker & Feature Roadmap](https://github.com/tandem-controls/tandem/issues)

---

## License

This project is licensed under the [MIT License](LICENSE).

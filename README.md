# T.A.N.D.E.M.

**T**win **A**xes, **N**o **D**rift, **E**xcept **M**aybe

[![CI](https://github.com/Bibi2424/TANDEM/actions/workflows/ci.yml/badge.svg)](https://github.com/Bibi2424/TANDEM/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Build: PlatformIO](https://img.shields.io/badge/Build-PlatformIO-orange.svg)](platformio.ini)

A DIY dual-joystick (HOSAS) controller system built around a portable, hardware-independent C++17 core (`tandem_core`) so connectivity modes can be explored independently and moved across microcontrollers without rewriting input math.

---

## Architecture Highlights

- **MCU-Portable by Design:** Final MCU selection is still open (**ESP32-S3** is a leading candidate, not yet locked in). Early prototyping runs on available **ESP32-S2** and **ESP32-WROOM** dev boards with hardware specifics isolated behind per-board headers.
- **Shared Input Pipeline (`lib/tandem_core`):** Hardware- and transport-independent C++17 core targeting per-axis calibration (`min`/`center`/`max` + clamping), smooth boundary-rescaled deadzones, sign-preserving response curves, and link-loss failsafe handling — verified with native host unit tests.
- **Modular Connectivity Modes (Explored Independently First):** Each transport mode is prototyped and validated as a standalone app under `src/apps/` before deciding whether or how to unify them:

| Mode | Transport / Feature | Required Capabilities | Initial Prototyping Target |
| :--- | :--- | :--- | :--- |
| **Mode A** | Direct BLE HID gamepad to PC | BLE | `ESP32-WROOM` |
| **Mode B** | Low-latency ESP-NOW + USB Base Station *(up to 1000 Hz USB HID)* | Wi-Fi / ESP-NOW + Native USB *(base)* | `ESP32-WROOM` → `ESP32-S2` |
| **Mode C** | Wired USB HID gamepad | Native USB | `ESP32-S2` |
| **Config** | Custom BLE GATT (Web Bluetooth) + validated NVS persistence | BLE + non-volatile storage | `ESP32-WROOM` |

- **Power-Path Safety:** Target dual-stick hardware pairs native USB with a proper LiPo/Li-ion charging and power-path protection circuit (never wiring battery voltage directly to 5V VBUS).

---

## Repository Structure

```text
TANDEM/
├── .github/workflows/ci.yml   # GitHub Actions CI (native tests + firmware builds)
├── docs/requirements.md       # Baseline hardware, firmware, and verification requirements
├── include/boards/            # Per-board capability flags and ADC1/GPIO pinmaps
├── lib/tandem_core/           # Pure, dependency-free C++17 core library (host-testable)
├── src/apps/bringup/          # Standalone firmware apps selected via build_src_filter
├── test/                      # Native host Unity unit tests (core, axis shaping, input pipeline)
├── platformio.ini             # PlatformIO environments (native, ESP32-WROOM, ESP32-S2)
├── AGENTS.md                  # Repository development & coding conventions
└── LICENSE                    # MIT License
```

---

## Getting Started

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) (`pio`)
- A host C++17 compiler (`g++` / `clang++`) for running native unit tests

### Build & Test

```bash
# Run native host unit tests for tandem_core
pio test -e native

# Build all default firmware environments
pio run

# Build a specific board bring-up target
pio run -e esp32_wroom_bringup
pio run -e esp32_s2_bringup

# Flash and open the serial console (115200 baud)
pio run -e esp32_s2_bringup -t upload
pio device monitor -b 115200   # add -p <PORT> if multiple boards are connected
```

---

## Documentation & Roadmap

- [Technical Requirements](docs/requirements.md)
- [Development & Agent Guidelines](AGENTS.md)
- [Issue Tracker & Feature Roadmap](https://github.com/Bibi2424/TANDEM/issues)

---

## License

This project is licensed under the [MIT License](LICENSE).

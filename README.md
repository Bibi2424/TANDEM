# T.A.N.D.E.M.

**T**win **A**xes, **N**o **D**rift, **E**xcept **M**aybe

A DIY dual-joystick (HOSAS) controller system designed to stay MCU-portable during development (**ESP32-S3** is a leading candidate, not yet locked in; early prototyping starts on available **ESP32-S2** and **ESP32-WROOM** dev boards).

## Overview

- **Matched Dual-Stick Hardware:** Two sticks sharing a unified, MCU-portable firmware codebase supporting analog axes, buttons/hats/switches, native USB (on capable targets), and LiPo/Li-ion power-path protection.
- **Modular Connectivity Modes (Explored Independently First):** Transport modes prototyped and validated individually before deciding whether or how to unify them:
  - **Wired USB HID:** Direct USB gamepad output (and charging) on native-USB-capable targets.
  - **Direct BLE HID:** Dongle-free wireless gamepad reports directly to the host OS on BLE-capable targets.
  - **Low-Latency Custom Link (ESP-NOW + USB Base Station):** Compact binary packets over ESP-NOW aggregated by a companion native-USB Base Station targeting up to 1000 Hz (1 ms) USB HID polling.
- **Shared Input Pipeline:** Common calibration (min/center/max), smooth boundary-rescaled deadzones, configurable response curves, and link-loss failsafe handling.
- **Configuration & Persistence:** Custom BLE GATT service (on BLE-capable targets) for Web Bluetooth / app tuning, backed by validated non-volatile storage (e.g., ESP32 NVS) with factory-reset fallback.

## Documentation

- [Technical Requirements](docs/requirements.md)

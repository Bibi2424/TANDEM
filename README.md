# T.A.N.D.E.M.

**T**win **A**xes, **N**o **D**rift, **E**xcept **M**aybe

A DIY dual-joystick (HOSAS) controller system built on matched **ESP32-S3** microcontrollers.

## Overview

- **Matched Dual-Stick Hardware:** Two sticks built around ESP32-S3 MCUs sharing a unified firmware codebase, supporting analog axes, buttons/hats/switches, native USB, and LiPo/Li-ion power-path protection.
- **Tri-Mode Connectivity:** Cleanly separated, mutually exclusive transport modes:
  - **Wired USB HID:** Direct USB gamepad output (and charging).
  - **Direct BLE HID:** Dongle-free wireless gamepad reports directly to the host OS.
  - **Low-Latency Custom Link (ESP-NOW + USB Base Station):** Compact binary packets over ESP-NOW aggregated by a companion USB Base Station targeting up to 1000 Hz (1 ms) USB HID polling.
- **Shared Input Pipeline:** Common calibration (min/center/max), smooth boundary-rescaled deadzones, configurable response curves, and link-loss failsafe handling.
- **Configuration & Persistence:** Custom BLE GATT service for Web Bluetooth / app tuning, backed by validated NVS storage with factory-reset fallback.

## Documentation

- [Technical Requirements](docs/requirements.md)

# **DIY Dual-Joystick Controller Requirements**

# **Document Overview**

This document outlines the complete technical baseline requirements for the hardware, firmware, communication transports, signal processing, and tooling of the DIY Dual-Joystick Controller system.

---

# **1\. System & Hardware Architecture Requirements**

* **Dual-Stick Matched Hardware:** Build two joysticks using ESP32-S3 microcontrollers with identical (or nearly identical) input hardware so the same firmware codebase can target both sticks without major forks.  
* **Analog & Digital Inputs:** Each stick supports analog axes (such as X/Y stick axes, throttle/slider, or twist—typically 2 to 4+ analog channels per stick, well within the ESP32-S3's 12-bit SAR ADC capability) plus digital buttons/hats/switches.  
* **Native USB & Power Safety:** Each stick exposes native ESP32-S3 USB (GPIO19 / GPIO20) for wired HID play, firmware flashing, and serial debugging, paired with a proper LiPo/Li-ion charging and power-path/protection circuit (never tying battery voltage directly to 5V VBUS).  
* **Optional USB Base Station:** A companion MCU/ESP32-S3 connected to the PC via USB to receive low-latency packets from both sticks and present unified or dual USB HID gamepads when operating in custom-link mode.

---

# **2\. Connectivity & Transport Modes**

* **Mutually Exclusive Radio Modes:** Firmware cleanly separates BLE HID and ESP-NOW into distinct states (shutting down one radio before starting the other) to avoid 2.4 GHz radio contention, coexistence jitter, and unnecessary power draw.  
* **Mode A — Direct BLE HID to PC:** Each stick can advertise and send standard BLE HID gamepad reports directly to the host OS when dongle-free wireless convenience is preferred.  
* **Mode B — Low-Latency Custom Link to USB Base Station:** Each stick can transmit compact binary packets over ESP-NOW to a USB base station, which aggregates inputs and forwards them over USB HID at high polling rates (targeting up to 1000 Hz / 1 ms on the USB side).  
* **Mode C — Wired USB HID:** Plugging into USB can support direct USB HID gamepad output (and charging), with clear priority/switching rules relative to wireless modes.

---

# **3\. Input Processing & Axis Shaping**

* **Shared Processing Pipeline:** Raw ADC samples and button states pass through a common input stage before reaching any transport (BLE HID, ESP-NOW, or USB HID):  
  * **Calibration:** Hardware calibration (min, center, max bounds per axis) and clamping.  
  * **Deadzones:** Configurable deadzones with smooth boundary rescaling so output doesn't jump when leaving the deadzone.  
  * **Response Curves:** Configurable axis response curves (linear, power/exponential blend, or custom curve parameters) while preserving sign.  
* **Failsafe & Link-Loss Handling:** In multi-stick / wireless modes, dropped or timed-out packets from a stick must trigger predictable failsafe behavior (such as centering affected axes or flagging disconnect) rather than holding stale/glitchy values.

---

# **4\. Configuration, Storage & Tooling**

* **Custom BLE GATT Configuration:** Expose a custom BLE GATT service and characteristics allowing a configuration tool (such as a Web Bluetooth page in Chrome/Edge or a custom app) to read/write calibration, deadzone, and curve parameters.  
* **Non-Volatile Persistence (NVS):** Save validated settings to ESP32 NVS (Preferences) with version/magic/checksum checks and a safe factory-reset fallback on boot.  
* **Stable HID Descriptors:** Keep HID report descriptors consistent when tuning curves/deadzones so existing OS controller bindings aren't broken by routine calibration changes.  
* **Maintainable Build Workflow:** Organize code into modular C++ components (input/ADC, curve math, BLE config/NVS, BLE HID, ESP-NOW, USB HID) in PlatformIO (using Arduino framework) or Arduino-ESP32, with separate build environments/targets for the stick and base station.

---

# **5\. Verification & Quality Targets**

* **ADC & Noise Hygiene:** Calibrated rest states stay centered with minimal jitter; analog traces/power rails are kept clean from RF and switching noise.  
* **Wireless & Link Reliability:** Sustained packet exchange under normal 2.4 GHz activity (Wi-Fi / USB 3.0 nearby) with observable drop rates and graceful recovery after disconnect/re-range.  
* **End-to-End Responsiveness:** Smooth, artifact-free movement in OS/browser gamepad testers across USB HID, BLE HID, and ESP-NOW \+ USB base station modes, with settings surviving power cycles.

# **DIY Dual-Joystick Controller Requirements**

# **Document Overview**

This document outlines the technical baseline requirements for the hardware, firmware, communication transports, signal processing, and tooling of the DIY Dual-Joystick Controller system. Final MCU selection and mode unification remain open while individual transports and hardware options are explored.

---

# **1\. System & Hardware Architecture Requirements**

* **Dual-Stick Matched Hardware:** Build two joysticks using matched microcontrollers with identical (or nearly identical) input hardware so the same firmware codebase can target both sticks without major forks. The final MCU is not locked in yet (**ESP32-S3** is currently a leading candidate, not a committed choice).
* **Early Prototyping Hardware (Board-Dependent Feature Subsets):** Initial prototyping starts on available dev boards, with each board exercising the features its silicon supports:
  * **ESP32-S2:** Native USB + Wi-Fi/ESP-NOW (no BLE) — suitable for wired USB HID and USB base-station prototyping.
  * **ESP32-WROOM (classic ESP32):** BLE + Wi-Fi/ESP-NOW (no native USB) — suitable for BLE HID, BLE GATT configuration, and ESP-NOW stick prototyping.
* **Analog & Digital Inputs:** Each stick supports analog axes (such as X/Y stick axes, throttle/slider, or twist—typically 2 to 4+ analog channels per stick, well within typical 12-bit SAR ADC capabilities) plus digital buttons/hats/switches.
* **Native USB & Power Safety:** On native-USB-capable targets (e.g., ESP32-S2 / ESP32-S3 on GPIO19 / GPIO20, or equivalent native USB pins on another MCU), expose native USB for wired HID play, firmware flashing, and serial debugging, paired with a proper LiPo/Li-ion charging and power-path/protection circuit (never tying battery voltage directly to 5V VBUS).
* **Optional USB Base Station:** A companion native-USB-capable MCU (e.g., ESP32-S2 for prototyping, or another suitable final MCU) connected to the PC via USB to receive low-latency packets from both sticks and present unified or dual USB HID gamepads when operating in custom-link mode.

---

# **2\. Connectivity & Transport Modes**

* **Independent Mode Exploration First:** Treat connectivity modes as independent prototypes/modules to explore and validate individually first (matching available prototyping board capabilities) before deciding whether and how to unify them into a single firmware.
* **Mutually Exclusive Radio Modes (when combined on one MCU):** If BLE and ESP-NOW are later combined on a single MCU, cleanly separate them into distinct states (shutting down one radio before starting the other) to avoid 2.4 GHz radio contention, coexistence jitter, and unnecessary power draw.
* **Mode A — Direct BLE HID to PC:** On BLE-capable targets, each stick can advertise and send standard BLE HID gamepad reports directly to the host OS when dongle-free wireless convenience is preferred.
* **Mode B — Low-Latency Custom Link to USB Base Station:** Each stick can transmit compact binary packets over ESP-NOW (or an equivalent low-latency link) to a native-USB base station, which aggregates inputs and forwards them over USB HID at high polling rates (targeting up to 1000 Hz / 1 ms on the USB side).
* **Mode C — Wired USB HID:** On native-USB-capable targets, plugging into USB can support direct USB HID gamepad output (and charging), with clear priority/switching rules relative to wireless modes if/when modes are combined.

---

# **3\. Input Processing & Axis Shaping**

* **Shared Processing Pipeline:** Raw ADC samples and button states pass through a common, hardware- and transport-independent input stage before reaching any transport (BLE HID, ESP-NOW, or USB HID):
  * **Calibration:** Hardware calibration (min, center, max bounds per axis) and clamping.
  * **Deadzones:** Configurable deadzones with smooth boundary rescaling so output doesn't jump when leaving the deadzone.
  * **Response Curves:** Configurable axis response curves (linear, power/exponential blend, or custom curve parameters) while preserving sign.
* **Failsafe & Link-Loss Handling:** In multi-stick / wireless modes, dropped or timed-out packets from a stick must trigger predictable failsafe behavior (such as centering affected axes or flagging disconnect) rather than holding stale/glitchy values.

---

# **4\. Configuration, Storage & Tooling**

* **Transport-Independent Configuration & Capabilities (BLE GATT + USB CDC Serial):** Expose a shared configuration interface and device-capability query (active axes/buttons/hat masks, live raw/shaped inspection, and calibration/deadzone/curve tuning) over **USB CDC Serial (WebSerial)** on all targets (including non-BLE `ESP32-S2`) and **custom BLE GATT** (Web Bluetooth) on BLE-capable targets.
* **Non-Volatile Persistence:** Save validated settings to non-volatile storage (e.g., ESP32 NVS / Preferences, or equivalent on another MCU) with version/magic/checksum checks and a safe factory-reset fallback on boot.
* **Stable HID Descriptors:** Keep HID report descriptors consistent when tuning curves/deadzones so existing OS controller bindings aren't broken by routine calibration changes.
* **Maintainable Build Workflow:** Organize code into modular, MCU-portable C++ components (pure core input/curve math and codecs separated from board/HAL adapters and transport modules: BLE config/NVS, BLE HID, ESP-NOW, USB HID) in PlatformIO (e.g., Arduino framework) with separate build environments/targets per board, experiment, stick, and base station.

---

# **5\. Verification & Quality Targets**

* **ADC & Noise Hygiene:** Calibrated rest states stay centered with minimal jitter; analog traces/power rails are kept clean from RF and switching noise.
* **Wireless & Link Reliability:** Sustained packet exchange under normal 2.4 GHz activity (Wi-Fi / USB 3.0 nearby) with observable drop rates and graceful recovery after disconnect/re-range.
* **End-to-End Responsiveness:** Smooth, artifact-free movement in OS/browser gamepad testers across each supported mode (USB HID, BLE HID, and ESP-NOW + USB base station) on its target hardware, with settings surviving power cycles.

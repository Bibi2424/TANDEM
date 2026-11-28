# **Tandem Controls — Technical Requirements**

# **Document Overview**

This document outlines the technical baseline requirements for the hardware, firmware, communication transports, signal processing, and tooling of **Tandem Controls** — a modular controller platform supporting single-stick, HOTAS, HOSAS, and custom hybrid rigs (button boxes, throttles, pedals). Final MCU selection and mode unification remain open while individual transports and hardware options are explored.

---

# **1\. System & Hardware Architecture Requirements**

* **Modular Single- & Dual-Stick Hardware:** Support both standalone single-device setups and multi-stick rigs (such as matched HOSAS joysticks or HOTAS/custom hybrid rigs) using compatible microcontrollers so the same firmware codebase serves all nodes without major forks. The final MCU is not locked in yet (**ESP32-S3** is currently a leading candidate, not a committed choice).
* **Early Prototyping Hardware (Board-Dependent Feature Subsets):** Initial prototyping starts on available dev boards, with each board exercising the features its silicon supports:
  * **ESP32-S2:** Native USB + Wi-Fi/ESP-NOW (no BLE) — suitable for wired USB HID and USB base-station prototyping.
  * **ESP32-WROOM (classic ESP32):** BLE + Wi-Fi/ESP-NOW (no native USB) — suitable for BLE HID, BLE GATT configuration, and ESP-NOW stick prototyping.
* **Analog & Digital Inputs:** Each stick supports analog axes (such as X/Y stick axes, throttle/slider, or twist—typically 2 to 4+ analog channels per stick, well within typical 12-bit SAR ADC capabilities) plus digital buttons/hats/switches.
* **Native USB & Power Safety:** On native-USB-capable targets (e.g., ESP32-S2 / ESP32-S3 on GPIO19 / GPIO20, or equivalent native USB pins on another MCU), expose native USB for wired HID play, firmware flashing, and serial debugging, paired with a proper LiPo/Li-ion charging and power-path/protection circuit (never tying battery voltage directly to 5V VBUS). For battery-powered wireless builds, use ultra-low-$I_q$ voltage regulation ($< 50\ \mu\text{A}$, avoiding ~5 mA 1117-class LDOs) and MOSFET power-gating for active analog sensors (pots/Hall/TMR) and status LEDs during Deep Sleep.
* **Optional USB Base Station:** A companion native-USB-capable MCU (e.g., ESP32-S2 for prototyping, or another suitable final MCU) connected to the PC via USB to receive low-latency packets from both sticks and present unified or dual USB HID gamepads when operating in custom-link mode.

---

# **2\. Connectivity & Transport Modes**

* **Independent Mode Exploration First:** Treat connectivity modes as independent prototypes/modules to explore and validate individually first (matching available prototyping board capabilities) before deciding whether and how to unify them into a single firmware.
* **Mutually Exclusive Radio Modes (when combined on one MCU):** If BLE and ESP-NOW are later combined on a single MCU, cleanly separate them into distinct states (shutting down one radio before starting the other) to avoid 2.4 GHz radio contention, coexistence jitter, and unnecessary power draw.
* **Mode A — Direct BLE HID to PC:** On BLE-capable targets, each stick can advertise and send standard BLE HID gamepad reports directly to the host OS when dongle-free wireless convenience is preferred.
* **Mode B — Low-Latency Custom Link to USB Base Station:** Each stick can transmit compact binary packets over unicast ESP-NOW (or an equivalent low-latency link with manual channel selection and pairing-time channel-quality assessment) to a native-USB base station, which aggregates inputs and forwards them over USB HID at high polling rates (targeting up to 1000 Hz / 1 ms on the USB side). Battery-powered sticks target connectionless Modem Sleep / dynamic `RX_READY` duty-cycling, 80 MHz CPU scaling, high PHY data rates (11–54 Mbps), controlled TX power (5–11 dBm), and adaptive Eco/Performance/Idle/Deep Sleep tiers.
* **Mode C — Wired USB HID:** On native-USB-capable targets, plugging into USB can support direct USB HID gamepad output (and charging), with clear priority/switching rules relative to wireless modes if/when modes are combined.

---

# **3\. Input Processing & Axis Shaping**

* **Strict Stage 1 / Stage 2 Pipeline Separation:**
  * **Stage 1 (Hardware Front-End):** Per-axis calibration (`min`, `center`, `max`, sensor-agnostic offsets for pots/Hall/TMR/encoders), ADC oversampling/adaptive noise filtering, open/short wire fault detection, and eager-actuate / defer-clear digital button debouncing run as an isolated front-end stage producing normalized axes (`int16_t` `-32767..+32767`) and a debounced button bitmask.
  * **Stage 2 (Logical Shaping & Routing):** Consumes only normalized Stage 1 outputs to apply configurable inner center deadzones + dynamic deadband, **outer saturation/edge deadzones** (plus optional anti-deadzone injection), sign-preserving **response curves** (`Linear`, `Expo`, `Power`/`PowerBlend`, `S-curve`, and host-sampled multi-point piecewise-linear LUTs), inverted switches, latching-to-momentary pulse emulation, and logical input-to-HID remapping (preparing cleanly for Post-1.0 Visual Node Graph execution on standalone sticks or the USB Base Station).
* **Failsafe & Link-Loss Handling:** In multi-stick / wireless modes, dropped or timed-out packets from a stick must trigger predictable failsafe behavior (such as centering affected axes or flagging disconnect) rather than holding stale/glitchy values, while coordinating timeouts cleanly with idle/heartbeat transmission intervals (`kEspNowFlagIdle`).

---

# **4\. Configuration, Storage & Tooling**

* **Primary Unified Config Transport (Vendor Raw HID / WebHID) & Fallbacks:**
  * **WebHID (Primary Unified Channel):** Expose a Vendor-Defined Raw HID Top-Level Collection (`Input`/`Output` reports only — avoiding HID Feature Reports due to OS Bluetooth stack bugs and latency) across native USB, BLE HID (HOGP), and the ESP-NOW Base Station config tunnel.
  * **Secondary / Fallback Channels:** Retain **USB CDC Serial (WebSerial)** for scripting/debug console access and **custom BLE GATT (`BLE_CFG` / Web Bluetooth)** for unpaired or mobile/Android configuration.
* **Single-Pipe Binary Protocol, Telemetry & Capability Handshake:**
  * Support atomic parameter updates and host-paced offset-chunked bulk transfers (`READ_CHUNK`/`WRITE_CHUNK` + CRC-32), compact binary opt-in live telemetry guarded by a browser heartbeat timeout (3–5 s), live RAM tuning with explicit flash commit, and a hardware boot-time factory-recovery chord.
  * Implement a capability-driven handshake (`GET_CAPABILITIES` / `caps` reporting `device_role`, `max_profiles`, `feature_flags`, `input_groups`/`output_groups`) and reserve Protocol v2 OpCode ranges (`0x40–0x4F` Profiles, `0x50–0x5F` Node Graph/Layout, `0x60–0x6F` Dongle Proxy) for Post-1.0 features (Selectable Output Device Profiles and the Visual Node Graph & Dual-Stick Multi-Profile Engine).
* **Forward-Compatible Non-Volatile Persistence:** Isolate fixed Stage 1 hardware calibration from user Stage 2 profiles (`schema_version` + `profile_slot`) in non-volatile storage using versioned, CRC-32-validated zero-heap TLV serialization (skipping unknown tags and defaulting omitted fields) with a safe factory-reset fallback on boot.
* **Stable HID Descriptors:** Keep HID report descriptors consistent when tuning curves/deadzones so existing OS controller bindings aren't broken by routine calibration changes.
* **Maintainable Build Workflow:** Organize code into modular, MCU-portable C++ components (pure core input/curve math and codecs separated from board/HAL adapters and transport modules: BLE config/NVS, BLE HID, ESP-NOW, USB HID) in PlatformIO (e.g., Arduino framework) under `firmware/` with separate build environments/targets per board, experiment, stick, and base station.

---

# **5\. Verification & Quality Targets**

* **ADC & Noise Hygiene:** Calibrated rest states stay centered with minimal jitter; analog traces/power rails are kept clean from RF and switching noise.
* **Wireless & Link Reliability:** Sustained packet exchange under normal 2.4 GHz activity (Wi-Fi / USB 3.0 nearby) with observable drop rates and graceful recovery after disconnect/re-range.
* **End-to-End Responsiveness:** Smooth, artifact-free movement in OS/browser gamepad testers across each supported mode (USB HID, BLE HID, and ESP-NOW + USB base station) on its target hardware, with settings surviving power cycles.

# T.A.N.D.E.M. User Quick-Start Guide

This guide walks through flashing **T.A.N.D.E.M.** firmware, wiring and inspecting dev-board inputs, switching transport modes, and calibrating sticks in the Web Configurator.

> [!CAUTION]
> **Power-Path Safety:** Never connect LiPo/Li-ion battery voltage directly to 5V USB `VBUS`. When powering sticks from a battery alongside USB, always use a proper LiPo/Li-ion charging, protection, and power-path management circuit.

---

## 1. Supported Prototyping Boards & Environments

T.A.N.D.E.M. runs board-dependent feature subsets so prototypes can be exercised across available ESP32 hardware:

| Board / MCU | Role | Capabilities | Default Mode | PlatformIO Env(s) | `esptool --chip` |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **ESP32-WROOM** (`esp32dev`) | Left / Right Stick | BLE (`BLE_HID`, `BLE_CFG`), Wi-Fi / ESP-NOW *(no native USB; USB-UART serial)* | `BLE_HID` | `esp32_wroom_stick` (Left)<br>`esp32_wroom_stick_right` (Right) | `esp32` |
| **ESP32-S2** (`lolin_s2_mini`) | Stick | Native USB (`USB_HID`, `WebHID`, `USB_SERIAL`), Wi-Fi / ESP-NOW *(no BLE)* | `USB_HID` | `esp32_s2_stick` | `esp32s2` |
| **ESP32-S2** (`lolin_s2_mini`) | Base Station | ESP-NOW RX -> USB HID (Dual `2x` or Combined `1x`) + WebHID / `USB_SERIAL` | `ESPNOW_RX_DUAL` / `ESPNOW_RX_COMBINED` | `esp32_s2_base_station` (Dual)<br>`esp32_s2_base_station_combined` (Combined) | `esp32s2` |
| **ESP32-S3** (`esp32-s3-devkitc-1`) | Stick *(candidate)* | Native USB + BLE + Wi-Fi / ESP-NOW *(all 4 stick modes)* | `USB_HID` | `esp32_s3_stick` | `esp32s3` |
| **ESP32-S3** (`esp32-s3-devkitc-1`) | Base Station *(candidate)* | ESP-NOW RX -> Dual `2x` USB HID + WebHID / `USB_SERIAL` | `ESPNOW_RX_DUAL` | `esp32_s3_base_station` | `esp32s3` |

---

## 2. Flashing Firmware

### Option A: Prebuilt Merged Release Binaries (Recommended for End Users)

GitHub Releases attach one merged, self-contained `.bin` per target (containing bootloader, partition table, `boot_app0`, and application) along with `SHA256SUMS.txt`.

1. Download the `.bin` matching your board/role and `SHA256SUMS.txt` from [GitHub Releases](https://github.com/Bibi2424/TANDEM/releases).
2. Verify download integrity and flash starting at offset **`0x0`**:

   ```bash
   sha256sum -c SHA256SUMS.txt
   esptool --chip <esp32|esp32s2|esp32s3> --port <PORT> write-flash 0x0 tandem-<env>-v<version>-merged.bin
   ```

   *(Use `--chip esp32` for ESP32-WROOM, `--chip esp32s2` for ESP32-S2 Mini, and `--chip esp32s3` for ESP32-S3.)*

### Option B: Build & Flash from Source (PlatformIO)

1. Install [PlatformIO Core](https://platformio.org/install/cli) (`pio`).
2. Build and upload your target environment:

   ```bash
   # Stick firmware
   pio run -e esp32_wroom_stick -t upload
   pio run -e esp32_wroom_stick_right -t upload
   pio run -e esp32_s2_stick -t upload
   pio run -e esp32_s3_stick -t upload

   # USB Base Station firmware (for ESP-NOW wireless mode)
   pio run -e esp32_s2_base_station -t upload
   pio run -e esp32_s2_base_station_combined -t upload
   pio run -e esp32_s3_base_station -t upload

   # Optional: generate a local 0x0-flashable merged binary
   pio run -e esp32_s2_stick -t mergebin
   ```

3. Open the serial console at **115200 baud**:

   ```bash
   pio device monitor -b 115200   # add -p <PORT> if multiple boards are connected
   ```

> [!TIP]
> - **Multiple boards attached:** Pin `upload_port` / `monitor_port` per environment in a gitignored `platformio_local.ini` file (see [README.md](../README.md#local-port-overrides-platformio_localini)).
> - **ESP32-S2 USB bootloader / serial tip:** If a native-USB `ESP32-S2` does not enter flash mode automatically, hold `BOOT` (`GPIO0`), tap `RST`, then release `BOOT`. Because native USB CDC re-enumerates on reset, tap `RST` after attaching the serial monitor if you want to capture the boot banner.

---

## 3. Prototyping Pinout Summary

Default pin assignments live in [board_config.h](../include/boards/board_config.h) ([esp32_wroom.h](../include/boards/esp32_wroom.h), [esp32_s2.h](../include/boards/esp32_s2.h), [esp32_s3.h](../include/boards/esp32_s3.h)):

| Signal | ESP32-WROOM (`esp32dev`) | ESP32-S2 (`lolin_s2_mini`) | ESP32-S3 (`esp32-s3-devkitc-1`) | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Axis X / Y / Z / Throttle (ADC1, 12-bit)** | `GPIO34`, `GPIO35`, `GPIO32`, `GPIO33` | `GPIO1`, `GPIO2`, `GPIO3`, `GPIO4` | `GPIO1`, `GPIO2`, `GPIO4`, `GPIO5` | Use ADC1 only (ADC2 conflicts with Wi-Fi/ESP-NOW); WROOM `GPIO34`–`39` are input-only |
| **Buttons 0–3 (`INPUT_PULLUP`)** | `GPIO18`, `GPIO19`, `GPIO21`, `GPIO22` | `GPIO5`, `GPIO6`, `GPIO7`, `GPIO8` | `GPIO6`, `GPIO7`, `GPIO8`, `GPIO9` | Active-low switches wired cleanly to `GND` |
| **`BOOT` Button** | `GPIO0` | `GPIO0` | `GPIO0` | Active-low control button (input toggle / mode switch) |
| **Status LED** | `GPIO2` | `GPIO15` | `-1` *(disabled)* | Fast blink (`250 ms`) = `SYNTHETIC` / Base linked; slow blink (`1000 ms`) = `LIVE` / Base idle |
| **Reserved / Avoid** | Flash `GPIO6`–`11`, strapping `0/2/5/12/15` | USB `GPIO19/20`, strapping `0/45/46` | USB `GPIO19/20`, strapping `0/3/45/46` | Do not reassign native USB `D-`/`D+` (`19`/`20`) on S2/S3 |

**POV Hat & Extra Slots:** `InputScannerConfig` supports up to **8 analog axes** (`0`–`7`), **32 active-low buttons**, and a **4-pin active-low POV hat** (`N, E, S, W`). Extra axis slots and the 4-pin hat are disabled (`-1`) in the default prototyping board headers and can be enabled per board header.

> [!TIP]
> **Testing without potentiometers or buttons wired:** No analog hardware is required to verify firmware or host USB/BLE reports — toggle **Synthetic Input** via a short `BOOT` press or sending `t` over serial to generate clean sweeping test waveforms across axes, buttons, and POV hat.

---

## 4. Controls & Transport Mode Switching

T.A.N.D.E.M. enforces **strictly mutually exclusive wireless stacks** — switching modes cleanly tears down the previous radio (`BLE` or `Wi-Fi/ESP-NOW`) before starting the next.

### Stick Controls (`BOOT` Button & 115200 Serial)

| Trigger | Action | Notes |
| :--- | :--- | :--- |
| **`BOOT` short press** (`< 1 s`) or **`t`** | Toggle **`LIVE` <-> `SYNTHETIC`** | `SYNTHETIC` drives sweeping test axes, buttons, and hat |
| **`BOOT` long press** (`>= 1 s`) or **`m`** | Cycle to **next supported transport mode** | Skips modes unsupported by the active board |
| Serial **`1`** | Switch to **`USB_HID`** | Native USB boards only (`ESP32-S2`, `ESP32-S3`) |
| Serial **`2`** | Switch to **`BLE_HID`** | BLE boards only (`ESP32-WROOM`, `ESP32-S3`) |
| Serial **`3`** | Switch to **`ESPNOW_TX`** | All stick targets |
| Serial **`4`** | Switch to **`BLE_CFG`** | BLE boards only (`ESP32-WROOM`, `ESP32-S3`) |
| Serial **`i`** | Flip Stick ID **`LEFT` <-> `RIGHT`** | Persists to NVS (`"dev_cfg"`); restarts/updates active radio identity |

### Base Station Controls (`BOOT` Button & 115200 Serial)

| Trigger | Action | Notes |
| :--- | :--- | :--- |
| **`BOOT` long press** (`>= 1 s`) or **`m`** | Toggle **`Dual` <-> `Combined`** | Persists to NVS (`"dev_cfg"`) and reboots to re-enumerate USB HID |
| Serial **`1`** / **`2`** | Select **`Dual` (`1`)** or **`Combined` (`2`)** | Persists to NVS and reboots when changing descriptor layout |
| Web Configurator / `$TANDEM mode <DUAL\|COMBINED>` | Select Base Station output mode | `Reset Defaults` clears NVS back to board/build default |

> [!NOTE]
> Stick transport mode, Left/Right role, and Base Station output mode persist automatically in ESP32 NVS (`"tandem"` / `"dev_cfg"`) across power cycles and can also be updated from the Web Configurator top status bar (`$TANDEM mode` / `$TANDEM role`).

### Mode Cycle Order per Board

- **`ESP32-WROOM`:** `BLE_HID` -> `ESPNOW_TX` -> `BLE_CFG` -> `BLE_HID`
- **`ESP32-S2`:** `USB_HID` -> `ESPNOW_TX` -> `USB_HID`
- **`ESP32-S3`:** `USB_HID` -> `BLE_HID` -> `ESPNOW_TX` -> `BLE_CFG` -> `USB_HID`

### Transport Mode Details

- **`USB_HID` (Wired USB):** Enumerates as composite `TANDEM-Stick-XXXX` providing a 32-button, 8-axis, POV-hat USB HID joystick, a vendor **WebHID** configuration endpoint, and **USB CDC serial** (`115200` baud). CDC remains active even if switched to `ESPNOW_TX` or wireless modes.
- **`BLE_HID` (Direct Bluetooth Gamepad):** Advertises as `TANDEM-BLE-XXXX` for OS pairing in Windows/macOS/Linux Bluetooth settings.
- **`BLE_CFG` (Web Bluetooth Tuning):** Stops `BLE_HID` / ESP-NOW and advertises as `TANDEM-CFG-XXXX` using the custom T.A.N.D.E.M. GATT service. Connect directly inside Chrome/Edge via **Connect BLE** — **do not** pair `TANDEM-CFG-...` in Windows Bluetooth Settings.
- **`ESPNOW_TX` + USB Base Station *(Experimental in `v0.1.0-alpha`)*:**
  > [!WARNING]
  > **Experimental Status (`v0.1.0-alpha`):** In this release, `ESPNOW_TX` transmits **broadcast packets** (`FF:FF:FF:FF:FF:FF`) on **channel 1** at `250 Hz` (`-DTANDEM_ESPNOW_CHANNEL`, `-DTANDEM_ESPNOW_TX_HZ`) to any listening T.A.N.D.E.M. Base Station (`esp32_s2_base_station`, `esp32_s2_base_station_combined`, or `esp32_s3_base_station`). Unicast Base Station pairing and NVS peer-MAC persistence are tracked in GitHub Issue **#48** (advanced bench users can override a unicast target at compile time via `-DTANDEM_ESPNOW_PEER_MAC={...}`).
  - Base Station receives both Left (`esp32_wroom_stick`) and Right (`esp32_wroom_stick_right`) sticks, applies an independent `200 ms` per-stick failsafe (`-DTANDEM_FAILSAFE_TIMEOUT_MS=200`), and exposes either **2 independent USB HID gamepads** (`Dual` default, `VID=0x303A PID=0x80C4`) or **1 combined gamepad** (`esp32_s2_base_station_combined`, `VID=0x303A PID=0x80C5`) at up to `1000 Hz`.

---

## 5. Calibrating & Saving to NVS (Web Configurator)

Open **[https://bibi2424.github.io/TANDEM/](https://bibi2424.github.io/TANDEM/)** in desktop **Chrome** or **Edge** *(or serve locally with `python tools/configurator/serve.py` and open `http://localhost:8000/`)*.

> [!NOTE]
> Chrome or Edge is required for **Connect WebHID**, **Connect Serial**, and **Connect BLE**. The **Browser Gamepad API** inside **Live Tester (joy.cpl)** works in any browser supporting the Gamepad API.

### Step-by-Step Calibration Workflow

1. **Connect a Stick:**
   - **Native USB (`ESP32-S2` / `ESP32-S3`):** Click **Connect WebHID** *(recommended — works alongside an open serial monitor)* or **Connect Serial** *(close any terminal/monitor holding the COM port first)*.
   - **`ESP32-WROOM` (BLE):** Switch the stick to **`BLE_CFG`** (`BOOT` long-press or serial `'4'`) and click **Connect BLE**, or connect over USB-UART using **Connect Serial**.
   - Confirm the top status bar populates **Role**, **Board**, **Mode**, **Input**, **Firmware**, and **Transports**.
2. **Calibrate Axes (**Axes & Calibration** tab):**
   - Release the stick at mechanical rest and click **Capture Center (All)**.
   - Click **Start Min/Max Sweep**, move every enabled axis smoothly through its full mechanical extremes, and stop the sweep.
   - Adjust **Deadzone**, **Invert**, and response **Curve** (`Linear` or `Expo` blend) per axis.
   - Click **Apply All Axes** to preview changes in RAM on the device.
3. **Persist to Flash (NVS):**
   - Click **Save to Flash** to store the CRC-32-validated `StickInputConfig` (`"tandem"` / `"stick_cfg"`) and `DeviceSettings` (`"tandem"` / `"dev_cfg"`) in ESP32 NVS (runtime **Role**, **Transport Mode**, and **Base Station Output Mode** changes also auto-persist to NVS immediately).
   - Power-cycle the board and confirm the boot banner reports `Config NVS : loaded` / `Dev NVS : loaded`. Click **Reset Defaults** at any time to clear NVS and restore defaults.
4. **Verify Live Behavior (**Live Tester (joy.cpl)** tab):**
   - Switch between **Browser Gamepad API** (OS HID view) and **Device Stream** (`@TANDEM` live telemetry showing raw ADC vs shaped output, 2D X/Y crosshair, circularity trace, rest offset, ADC noise floor, buttons, and POV hat).
   - Connecting to a **Base Station** displays live dual/combined telemetry and a read-only calibration banner — connect directly to each stick to tune and save NVS settings.

# Tandem Controls — Release HIL Smoke-Test Checklist

Hardware-in-the-Loop (HIL) verification checklist required before cutting a tagged Tandem Controls release (per [AGENTS.md](../AGENTS.md) HIL gate and [requirements.md](requirements.md) §5).

> **Rule:** Never mark on-hardware behavior as verified from host tests or compilation builds alone. Run every applicable physical check below on real boards and record pass/fail evidence before tagging.

---

## Sign-Off Record

```text
Release Tag     : v<MAJOR.MINOR.PATCH[-prerelease]>
Git Commit SHA  : <7-char commit>
Tester          : <name / handle>
Date (UTC)      : <YYYY-MM-DD>
Host OS/Browser : <e.g. Windows 11 / Chrome xxx>
Boards Tested   : <e.g. ESP32-S2 Mini, ESP32-WROOM x2, ESP32-S3 DevKitC-1>
```

---

## 1. Host Pre-Flight & Release Artifacts

- [ ] Working tree is clean on target `main` commit; formatting passes:
  `python tools/check_format.py --strict-version`
- [ ] All native unit tests pass (`0` warnings):
  `pio test -d firmware -e native -v`
- [ ] Native coverage tests pass (`0` warnings):
  `pio test -d firmware -e native_cov`
- [ ] All default firmware environments build cleanly (`0` warnings):
  `pio run -d firmware`
- [ ] Merged `0x0` release binaries built and verified (via `pio run -d firmware -e <env> -t mergebin` or `.github/workflows/release.yml` `workflow_dispatch` dry run + `sha256sum -c SHA256SUMS.txt`).
- [ ] **Firmware identity check:** `$TANDEM caps` (and the Web Configurator **Firmware** badge) reports the exact build SemVer + Git commit (never `0.0.0-unknown` or `unknown`).

---

## 2. `ESP32-S2` Wired `USB_HID`, WebHID & WebSerial (`esp32_s2_stick`)

- [ ] Flash `esp32_s2_stick` (from `firmware-merged.bin` at `0x0` or `pio run -d firmware -e esp32_s2_stick -t upload`) and open serial monitor (`115200` baud, tapping `RST` after attaching).
- [ ] Boot banner prints expected board (`ESP32-S2 (LOLIN S2 Mini)`), `Caps : USB=1 BLE=0 ESP-NOW=1`, and `Mode : USB_HID (ok)`.
- [ ] Host OS enumerates `TANDEM-Stick-XXXX` cleanly (`VID=0x303A`, `PID=0x80C2` on LOLIN S2 Mini).
- [ ] **Synthetic input:** Short-press `BOOT` -> banner prints `SYNTHETIC`, LED blinks at `250 ms`; axes sweep smoothly and buttons cycle in Windows `joy.cpl`, [tools/hil_usb_hid_check.py](../tools/hil_usb_hid_check.py) (`python tools/hil_usb_hid_check.py --duration 4.0`), and Web Configurator **Live Tester (joy.cpl)** (**Browser Gamepad API**).
- [ ] **Live analog/digital input:** Short-press `BOOT` -> `LIVE`, LED blinks at `1000 ms`. Verify ADC1 rest centering, jitter/noise floor, full mechanical sweep, and active-low button responses (`GPIO5`–`GPIO8`) in **Live Tester (joy.cpl)**.
- [ ] **WebHID concurrency:** Click **Connect WebHID** in Chrome/Edge while USB HID joystick reports are streaming (with serial monitor open or closed); verify **Device Stream** raw/out telemetry, `get`, and `set` respond cleanly without stalling HID.
- [ ] **WebSerial:** Close any serial monitor, click **Connect Serial** at `115200` baud, verify `caps` / `get` / **Device Stream** work cleanly.
- [ ] **Mode switch & isolation:** Long-press `BOOT` (`>= 1 s`) -> cycles `USB_HID` -> `ESPNOW_TX` while USB CDC stays connected and Windows HID axes release cleanly to centered (`0`). Long-press `BOOT` again -> returns cleanly to `USB_HID`.

---

## 3. `ESP32-WROOM` `BLE_HID` & `BLE_CFG` (`esp32_wroom_stick` / `esp32_wroom_stick_right`)

- [ ] Flash Left (`esp32_wroom_stick`) and Right (`esp32_wroom_stick_right`) boards; confirm boot banners (`BLE Name : TANDEM-BLE-XXXX`, `Stick ID : LEFT (1)` / `RIGHT (2)`).
- [ ] **`BLE_HID` gameplay check:** Pair `TANDEM-BLE-XXXX` in OS Bluetooth settings, open `joy.cpl` and Web Configurator (**Browser Gamepad API**); toggle `SYNTHETIC` and `LIVE` via `BOOT` short-press and confirm smooth axis/button reporting with no stuck inputs or disconnects over >= 60 s.
- [ ] **`BLE_CFG` radio teardown & Web Bluetooth:** Long-press `BOOT` until mode reaches `BLE_CFG` (or send serial `'4'`); verify `BLE_HID` disconnects cleanly and board advertises `TANDEM-CFG-XXXX`.
- [ ] Without pairing `TANDEM-CFG-...` in OS Bluetooth settings, click **Connect BLE** in Chrome/Edge; verify **Role**, **Board**, **Firmware**, **Device Stream**, calibration sliders, and **Apply All Axes** work cleanly.
- [ ] Switch `BLE_CFG` -> `BLE_HID` (long-press `BOOT` twice on WROOM, passing `ESPNOW_TX`) and confirm clean radio teardown/re-init and automatic reconnection to paired host with zero crash/brownout.

---

## 4. Experimental ESP-NOW + USB Base Station (`Dual` & `Combined`)

> [!NOTE]
> In `v0.1.0-alpha`, ESP-NOW operates in **experimental broadcast-only mode** (channel `1`, `250 Hz`, `FF:FF:FF:FF:FF:FF`) ahead of unicast pairing (`#48`). Verify in a normal 2.4 GHz environment (Wi-Fi active nearby).

### 4A. Dual Gamepad Base Station (`esp32_s2_base_station`, `VID=0x303A PID=0x80C4`)

- [ ] Flash `esp32_s2_base_station` to `ESP32-S2`; verify boot banner prints `ESPNOW_RX_DUAL`, `failsafe=200 ms`, `ESP-NOW : READY`, and host sees **2 USB HID controllers**.
- [ ] Put both Left (`esp32_wroom_stick`) and Right (`esp32_wroom_stick_right`) sticks into **`ESPNOW_TX`** mode.
- [ ] Verify Base Station LED switches from slow idle (`1000 ms`) to fast connected (`250 ms`), serial status shows `L:CONN` and `R:CONN` with steady `rx` count, RSSI, and negligible drops over >= 2 minutes.
- [ ] Verify Left and Right inputs move their respective USB controllers independently in `joy.cpl` and Web Configurator (**Browser Gamepad API**).
- [ ] **Single-stick failsafe & recovery:** Power off (or switch mode on) Right stick only -> within `200 ms`, Right reports `TOUT` / centers to neutral while Left continues responding smoothly with zero glitching. Power Right stick back into `ESPNOW_TX` -> recovers to `R:CONN` automatically without resetting the Base Station.
- [ ] Repeat failsafe & recovery check with both sticks offline -> Base LED returns to idle blink (`1000 ms`) and telemetry reports failsafe active.
- [ ] **Base Station WebHID / WebSerial:** Connect Base Station in Web Configurator (**Connect WebHID** and **Connect Serial**); confirm read-only calibration banner displays and telemetry bar updates `Sample / RX Rate`, `TX / HID Rate`, `Link / Peer` (`L:CONN R:CONN`), `RSSI`, `Dropped`, and `Failsafe`.

### 4B. Combined Gamepad Base Station (`esp32_s2_base_station_combined`, `VID=0x303A PID=0x80C5`)

- [ ] Flash `esp32_s2_base_station_combined`; confirm host enumerates **1 combined USB HID controller**.
- [ ] Drive Left + Right sticks simultaneously (`SYNTHETIC` or `LIVE`) and verify Left axes/buttons/hat and Right axes/buttons map concurrently into the single controller without crosstalk, and single-stick disconnect centers only the disconnected stick's mapped axes.

---

## 5. NVS Persistence Across Power Cycles

- [ ] Connect a stick (`ESP32-S2` over **WebHID**/`WebSerial`, and `ESP32-WROOM` over **`BLE_CFG`**/`WebSerial`) in the Web Configurator.
- [ ] Capture center (**Capture Center (All)**), sweep min/max (**Start Min/Max Sweep**), set distinct non-default parameters (e.g., deadzone `0.12`, expo `0.40`, invert toggled on Axis 0), click **Apply All Axes**, then click **Save to Flash**.
- [ ] Disconnect USB and remove all power for >= **5 seconds**, then reconnect.
- [ ] Confirm serial boot banner prints `Config NVS : loaded` and reading config in Web Configurator (`get`) matches every saved calibration, deadzone, invert, and curve value verbatim.
- [ ] Click **Reset Defaults**, power-cycle >= **5 seconds**, reconnect, and confirm `Config NVS : not_found_defaults` and `Dev NVS : not_found_defaults` with factory defaults active.
- [ ] Verify selected transport mode, Left/Right stick role, and Base Station Dual/Combined mode also survive a full power cycle cleanly.

---

## 6. Optional `ESP32-S3` Bench Verification (`esp32_s3_stick` / `esp32_s3_base_station`)

*(Run whenever `ESP32-S3` hardware is on the bench.)*

- [ ] Flash `esp32_s3_stick` merged binary at `0x0` (confirms `0x0000` S3 bootloader offset); verify `USB_HID` + WebHID enumeration.
- [ ] Cycle all 4 stick modes (`USB_HID` -> `BLE_HID` -> `ESPNOW_TX` -> `BLE_CFG` -> `USB_HID`) via `BOOT` long-press and confirm clean radio transitions and NVS save/load.
- [ ] Flash `esp32_s3_base_station` merged binary at `0x0` and verify ESP-NOW Dual HID RX + WebHID telemetry.

---

## 7. Release Tagging & Final Artifact Check

- [ ] Confirm Milestone targets (all `v0.1.0-alpha` issues/PRs closed) and CI is green on `main`.
- [ ] Create and push annotated SemVer tag:

  ```bash
  git tag -a v<version> -m "Tandem Controls v<version>"
  git push origin v<version>
  ```

- [ ] Verify `.github/workflows/release.yml` finishes cleanly, download all 7 `.bin` assets + `SHA256SUMS.txt` from the published GitHub Release, and run `sha256sum -c SHA256SUMS.txt` + spot-flash at `0x0` with `esptool`.

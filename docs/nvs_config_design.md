# NVS Configuration Persistence Design

This document describes how T.A.N.D.E.M. validates and persists per-stick input configuration (`tandem::StickInputConfig`) in ESP32 Non-Volatile Storage (NVS).

## 1. Architectural Split

To keep core logic MCU-portable and host-testable (`AGENTS.md`), persistence is split into two layers:

1. **Pure Schema, Validation & Codec (`lib/tandem_core/`)**
   - [config_storage.h](../lib/tandem_core/include/tandem/config_storage.h) / [config_storage.cpp](../lib/tandem_core/src/config_storage.cpp)
   - Standard dependency-free C++17 (no Arduino or ESP-IDF headers).
   - Handles deterministic binary serialization, IEEE 802.3 CRC-32 calculation/verification, and semantic range validation.
   - Covered by native Unity host tests in [test/test_config_storage/test_main.cpp](../test/test_config_storage/test_main.cpp).

2. **ESP32 NVS Adapter (`lib/tandem_hal/`)**
   - [config_nvs.h](../lib/tandem_hal/include/tandem_hal/config_nvs.h) / [config_nvs.cpp](../lib/tandem_hal/src/config_nvs.cpp)
   - Scoped to `espressif32` and wraps Arduino-ESP32 `Preferences` (`nvs` flash partition).
   - Stores the serialized blob as a single atomic binary entry under namespace `"tandem"`, key `"stick_cfg"`.

---

## 2. Binary Blob Format (`TMFC` v1 — 204 Bytes)

Rather than writing raw C++ structs (`memcpy` of `StickInputConfig`), `encode_stick_config()` packs fields explicitly in **little-endian** byte order into `tandem::PersistedConfigBlob` (`uint8_t[204]`).

**Why explicit serialization instead of `memcpy`?**
- C++ structs contain compiler-inserted padding bytes (e.g., between `CurveType` / `bool` and `float`) whose contents may be uninitialized, causing non-deterministic CRC mismatches.
- Explicit little-endian packing guarantees identical layout across MCUs, compilers, and optimization levels.

### Layout

| Offset | Size | Field | Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0..3` | 4 | `magic` | `char[4]` | `"TMFC"` (`0x54, 0x4D, 0x46, 0x43`) |
| `4..5` | 2 | `schema_version` | `uint16_t` LE | `1` (`kConfigStorageVersion`) |
| `6` | 1 | `axis_count` | `uint8_t` | `8` (`kMaxAxes`) |
| `7` | 1 | `reserved` | `uint8_t` | `0` |
| `8..199` | 192 | `axes[0..7]` | `8 × 24 B` | Per-axis records (see below) |
| `200..203` | 4 | `crc32` | `uint32_t` LE | IEEE 802.3 CRC-32 (`0xEDB88320`) over bytes `0..199` |

### Per-Axis Record (24 Bytes)

| Offset | Size | Field | Type | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `+0..3` | 4 | `min` | `int32_t` LE | Raw ADC minimum |
| `+4..7` | 4 | `center` | `int32_t` LE | Raw ADC rest center |
| `+8..11` | 4 | `max` | `int32_t` LE | Raw ADC maximum |
| `+12..15` | 4 | `deadzone` | `float` LE | IEEE-754 32-bit float in `[0.0, 1.0)` |
| `+16` | 1 | `curve_type` | `uint8_t` | `0` = `Linear`, `1` = `Expo` |
| `+17` | 1 | `flags` | `uint8_t` | Bit 0 = `enabled`, Bit 1 = `invert` |
| `+18..19` | 2 | `reserved` | `uint16_t` LE | `0` |
| `+20..23` | 4 | `curve_param` | `float` LE | IEEE-754 32-bit float in `[0.0, 1.0]` |

---

## 3. Validation & Fail-Safe Behavior

Both `encode_stick_config()` (before writing) and `decode_stick_config()` (after reading) enforce:

1. **Framing & Integrity (decode):**
   - Exact length (`204` bytes)
   - Magic bytes `"TMFC"`
   - Supported `schema_version` (`1`)
   - Matching `axis_count` (`8`)
   - Valid `crc32` over bytes `0..199`
2. **Semantic Validity (every axis `0..7`):**
   - `calibration.is_valid()` (`min < center < max`)
   - `deadzone` is finite and in `[0.0f, 1.0f)`
   - `curve.type` is a known `CurveType` (`Linear` or `Expo`)
   - `curve.param` is finite and in `[0.0f, 1.0f]`

If any check fails, `decode_stick_config()` returns an explicit `ConfigDecodeResult` error and leaves the caller's `StickInputConfig` untouched.

---

## 4. Runtime Lifecycle (`src/apps/stick/main.cpp`)

1. **Boot (`setup()`):**
   - Populates `g_input_config` with board-specific factory defaults (`make_default_input_config()`).
   - Calls `tandem::hal::load_stick_config(g_input_config)`.
   - If NVS contains a valid blob (`Loaded`), it overwrites `g_input_config`; if missing (`NotFound`), corrupt (`CorruptOrInvalid`), or unreadable (`OpenFailed`), the firmware logs the status and boots cleanly with factory defaults.
2. **Live Tuning (`$TANDEM set ...`):**
   - Updates `g_input_config` in RAM only (fast, no flash wear during slider dragging).
3. **Persist (`$TANDEM save`):**
   - Calls `tandem::hal::save_stick_config(g_input_config)` to validate, checksum, and commit the 204-byte blob to NVS.
4. **Factory Reset (`$TANDEM reset`):**
   - Restores `g_input_config` to `make_default_input_config()` and calls `tandem::hal::reset_stick_config()` to remove `"stick_cfg"` from NVS.

---

## 5. Schema Evolution

- Breaking changes to the persisted layout increment `kConfigStorageVersion`.
- Future firmware versions can either add a migration path for `schema_version == 1` in `decode_stick_config()` or reject older versions cleanly so the stick falls back to safe factory defaults rather than misinterpreting old bytes.

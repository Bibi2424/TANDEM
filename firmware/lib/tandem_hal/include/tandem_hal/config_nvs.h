// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

#include "tandem/config_storage.h"
#include "tandem/input_pipeline.h"

namespace tandem::hal {

enum class NvsLoadStatus : uint8_t {
  Loaded = 0,
  NotFound = 1,
  OpenFailed = 2,
  CorruptOrInvalid = 3,
};

/// Loads and validates `StickInputConfig` from ESP32 NVS ("tandem" / "stick_cfg").
/// On any non-Loaded status, `out` is left unmodified so caller defaults remain intact.
NvsLoadStatus load_stick_config(StickInputConfig& out) noexcept;

/// Validates, checksums, and persists `cfg` to ESP32 NVS.
bool save_stick_config(const StickInputConfig& cfg) noexcept;

/// Removes any persisted `StickInputConfig` from ESP32 NVS.
bool reset_stick_config() noexcept;

/// Loads and validates `DeviceSettings` from ESP32 NVS ("tandem" / "dev_cfg").
/// On any non-Loaded status, `out` is left unmodified so caller defaults remain intact.
NvsLoadStatus load_device_settings(DeviceSettings& out) noexcept;

/// Validates, checksums, and persists `settings` to ESP32 NVS.
bool save_device_settings(const DeviceSettings& settings) noexcept;

/// Removes any persisted `DeviceSettings` from ESP32 NVS.
bool reset_device_settings() noexcept;

/// Returns a short human-readable label for `NvsLoadStatus`.
const char* nvs_load_status_name(NvsLoadStatus status) noexcept;

}  // namespace tandem::hal

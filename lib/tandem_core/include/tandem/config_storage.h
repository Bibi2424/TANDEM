#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/input_pipeline.h"
#include "tandem/types.h"

namespace tandem {

// Versioned 204-byte little-endian persisted StickInputConfig contract:
//   Header (8 bytes):
//     [0..3]   magic ("TMFC" = 0x54, 0x4D, 0x46, 0x43)
//     [4..5]   schema_version (uint16_t LE, kConfigStorageVersion = 1)
//     [6]      axis_count (uint8_t, kMaxAxes = 8)
//     [7]      reserved (uint8_t, 0)
//   Axis records (kMaxAxes x 24 bytes = 192 bytes):
//     [0..3]   min (int32_t LE)
//     [4..7]   center (int32_t LE)
//     [8..11]  max (int32_t LE)
//     [12..15] deadzone (float IEEE-754 LE)
//     [16]     curve_type (uint8_t: 0 = Linear, 1 = Expo)
//     [17]     flags (uint8_t: bit0 = enabled, bit1 = invert)
//     [18..19] reserved (uint16_t LE, 0)
//     [20..23] curve_param (float IEEE-754 LE)
//   Trailer (4 bytes):
//     [200..203] crc32 (uint32_t LE, IEEE 802.3 CRC-32 over bytes [0..199])
inline constexpr uint8_t kConfigStorageMagic0 = 0x54U;  // 'T'
inline constexpr uint8_t kConfigStorageMagic1 = 0x4DU;  // 'M'
inline constexpr uint8_t kConfigStorageMagic2 = 0x46U;  // 'F'
inline constexpr uint8_t kConfigStorageMagic3 = 0x43U;  // 'C'
inline constexpr uint16_t kConfigStorageVersion = 1U;
inline constexpr size_t kConfigHeaderBytes = 8U;
inline constexpr size_t kConfigAxisRecordBytes = 24U;
inline constexpr size_t kConfigTrailerBytes = 4U;
inline constexpr size_t kPersistedConfigBytes = kConfigHeaderBytes + (kMaxAxes * kConfigAxisRecordBytes) + kConfigTrailerBytes;

struct PersistedConfigBlob {
  uint8_t bytes[kPersistedConfigBytes]{};
};

static_assert(sizeof(PersistedConfigBlob) == kPersistedConfigBytes, "PersistedConfigBlob must match kPersistedConfigBytes");
static_assert(kPersistedConfigBytes == 204U, "PersistedConfigBlob v1 layout must be exactly 204 bytes");

enum class ConfigDecodeResult : uint8_t {
  Ok = 0,
  InvalidLength = 1,
  BadMagic = 2,
  UnsupportedVersion = 3,
  InvalidAxisCount = 4,
  BadCrc = 5,
  InvalidConfig = 6,
};

/// Computes standard IEEE 802.3 CRC-32 (polynomial 0xEDB88320).
uint32_t crc32_ieee(const uint8_t* data, size_t len) noexcept;

/// Returns true when a single AxisConfig has valid bounds, deadzone, and curve parameters.
bool is_valid_axis_config(const AxisConfig& axis) noexcept;

/// Returns true when every axis slot in `cfg` passes `is_valid_axis_config`.
bool is_valid_stick_config(const StickInputConfig& cfg) noexcept;

/// Serializes and checksums `cfg` into `out`. Returns false if `cfg` fails validation.
bool encode_stick_config(const StickInputConfig& cfg, PersistedConfigBlob& out) noexcept;

/// Validates magic, version, axis count, CRC-32, and config bounds before updating `out`.
/// On any failure, `out` is left unmodified.
ConfigDecodeResult decode_stick_config(const uint8_t* data, size_t len, StickInputConfig& out) noexcept;

/// Returns a short human-readable name for `ConfigDecodeResult`.
const char* config_decode_result_name(ConfigDecodeResult result) noexcept;

// Versioned 16-byte little-endian persisted DeviceSettings contract:
//   Header (8 bytes):
//     [0..3]   magic ("TMDS" = 0x54, 0x4D, 0x44, 0x53)
//     [4..5]   schema_version (uint16_t LE, kDeviceSettingsStorageVersion = 1)
//     [6..7]   reserved (uint16_t LE, 0)
//   Payload (4 bytes):
//     [8]      transport_mode (uint8_t: 0 = UsbHid, 1 = BleHid, 2 = EspNowTx, 3 = BleConfig)
//     [9]      stick_id (uint8_t: 1 = Left, 2 = Right)
//     [10]     base_mode (uint8_t: 0 = Dual, 1 = Combined)
//     [11]     reserved (uint8_t, 0)
//   Trailer (4 bytes):
//     [12..15] crc32 (uint32_t LE, IEEE 802.3 CRC-32 over bytes [0..11])
inline constexpr uint8_t kDeviceSettingsMagic0 = 0x54U;  // 'T'
inline constexpr uint8_t kDeviceSettingsMagic1 = 0x4DU;  // 'M'
inline constexpr uint8_t kDeviceSettingsMagic2 = 0x44U;  // 'D'
inline constexpr uint8_t kDeviceSettingsMagic3 = 0x53U;  // 'S'
inline constexpr uint16_t kDeviceSettingsStorageVersion = 1U;
inline constexpr size_t kDeviceSettingsHeaderBytes = 8U;
inline constexpr size_t kDeviceSettingsPayloadBytes = 4U;
inline constexpr size_t kDeviceSettingsTrailerBytes = 4U;
inline constexpr size_t kPersistedDeviceSettingsBytes = kDeviceSettingsHeaderBytes + kDeviceSettingsPayloadBytes + kDeviceSettingsTrailerBytes;

struct BoardTransportCaps {
  bool has_native_usb{false};
  bool has_ble{false};
  bool has_espnow{false};
};

struct DeviceSettings {
  StickTransportMode transport_mode{StickTransportMode::UsbHid};
  StickId stick_id{StickId::Left};
  BaseOutputMode base_mode{BaseOutputMode::Dual};
};

struct PersistedDeviceSettingsBlob {
  uint8_t bytes[kPersistedDeviceSettingsBytes]{};
};

static_assert(sizeof(PersistedDeviceSettingsBlob) == kPersistedDeviceSettingsBytes,
              "PersistedDeviceSettingsBlob must match kPersistedDeviceSettingsBytes");
static_assert(kPersistedDeviceSettingsBytes == 16U, "PersistedDeviceSettingsBlob v1 layout must be exactly 16 bytes");

constexpr bool is_valid_transport_mode(StickTransportMode mode) noexcept {
  return mode == StickTransportMode::UsbHid || mode == StickTransportMode::BleHid || mode == StickTransportMode::EspNowTx ||
         mode == StickTransportMode::BleConfig;
}

constexpr bool is_valid_stick_id(StickId id) noexcept {
  return id == StickId::Left || id == StickId::Right;
}

constexpr bool is_valid_base_mode(BaseOutputMode mode) noexcept {
  return mode == BaseOutputMode::Dual || mode == BaseOutputMode::Combined;
}

/// Returns true when every field in `settings` holds a valid enum value.
bool is_valid_device_settings(const DeviceSettings& settings) noexcept;

/// Returns true when `mode` is valid and supported by `caps`.
constexpr bool is_transport_mode_supported(StickTransportMode mode, const BoardTransportCaps& caps) noexcept {
  switch (mode) {
    case StickTransportMode::UsbHid:
      return caps.has_native_usb;
    case StickTransportMode::BleHid:
    case StickTransportMode::BleConfig:
      return caps.has_ble;
    case StickTransportMode::EspNowTx:
      return caps.has_espnow;
  }
  return false;
}

/// Returns a copy of `settings` with any invalid or board-unsupported fields clamped to `defaults` (or safe fallback).
DeviceSettings clamp_device_settings(const DeviceSettings& settings, const BoardTransportCaps& caps, const DeviceSettings& defaults) noexcept;

/// Serializes and checksums `settings` into `out`. Returns false if `settings` fails validation.
bool encode_device_settings(const DeviceSettings& settings, PersistedDeviceSettingsBlob& out) noexcept;

/// Validates magic, version, CRC-32, and field validity before updating `out`.
/// On any failure, `out` is left unmodified.
ConfigDecodeResult decode_device_settings(const uint8_t* data, size_t len, DeviceSettings& out) noexcept;

}  // namespace tandem

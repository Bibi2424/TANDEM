#pragma once

#include <cstdint>

#include "boards/board_config.h"

namespace tandem::stick {

enum class StickTransportMode : uint8_t {
  UsbHid = 0,
  BleHid = 1,
  EspNowTx = 2,
  BleConfig = 3,
};

constexpr bool is_mode_supported(StickTransportMode mode) noexcept {
  switch (mode) {
    case StickTransportMode::UsbHid:
      return tandem::board::kHasNativeUsb;
    case StickTransportMode::BleHid:
    case StickTransportMode::BleConfig:
      return tandem::board::kHasBle;
    case StickTransportMode::EspNowTx:
      return tandem::board::kHasEspNow;
  }
  return false;
}

constexpr const char* mode_name(StickTransportMode mode) noexcept {
  switch (mode) {
    case StickTransportMode::UsbHid:
      return "USB_HID";
    case StickTransportMode::BleHid:
      return "BLE_HID";
    case StickTransportMode::EspNowTx:
      return "ESPNOW_TX";
    case StickTransportMode::BleConfig:
      return "BLE_CFG";
  }
  return "UNKNOWN";
}

constexpr StickTransportMode default_transport_mode() noexcept {
#if defined(TANDEM_DEFAULT_TRANSPORT_MODE)
  constexpr auto configured = static_cast<StickTransportMode>(TANDEM_DEFAULT_TRANSPORT_MODE);
  static_assert(is_mode_supported(configured), "TANDEM_DEFAULT_TRANSPORT_MODE is not supported on this board");
  return configured;
#else
  if constexpr (tandem::board::kHasNativeUsb) {
    return StickTransportMode::UsbHid;
  } else if constexpr (tandem::board::kHasBle) {
    return StickTransportMode::BleHid;
  } else {
    return StickTransportMode::EspNowTx;
  }
#endif
}

constexpr StickTransportMode next_supported_mode(StickTransportMode current) noexcept {
  constexpr StickTransportMode kOrder[4] = {
      StickTransportMode::UsbHid,
      StickTransportMode::BleHid,
      StickTransportMode::EspNowTx,
      StickTransportMode::BleConfig,
  };
  const uint8_t start = static_cast<uint8_t>(current);
  for (uint8_t offset = 1; offset <= 4; ++offset) {
    const StickTransportMode candidate = kOrder[(start + offset) % 4];
    if (is_mode_supported(candidate)) {
      return candidate;
    }
  }
  return current;
}

#if TANDEM_CAP_NATIVE_USB && !TANDEM_CAP_BLE && TANDEM_CAP_ESPNOW
static_assert(default_transport_mode() == StickTransportMode::UsbHid, "S2 default mode must be UsbHid");
static_assert(next_supported_mode(StickTransportMode::UsbHid) == StickTransportMode::EspNowTx, "S2 next mode from UsbHid must be EspNowTx");
static_assert(next_supported_mode(StickTransportMode::EspNowTx) == StickTransportMode::UsbHid, "S2 next mode from EspNowTx must be UsbHid");
#elif !TANDEM_CAP_NATIVE_USB && TANDEM_CAP_BLE && TANDEM_CAP_ESPNOW
static_assert(default_transport_mode() == StickTransportMode::BleHid, "WROOM default mode must be BleHid");
static_assert(next_supported_mode(StickTransportMode::BleHid) == StickTransportMode::EspNowTx, "WROOM next mode from BleHid must be EspNowTx");
static_assert(next_supported_mode(StickTransportMode::EspNowTx) == StickTransportMode::BleConfig, "WROOM next mode from EspNowTx must be BleConfig");
static_assert(next_supported_mode(StickTransportMode::BleConfig) == StickTransportMode::BleHid, "WROOM next mode from BleConfig must be BleHid");
#elif TANDEM_CAP_NATIVE_USB && TANDEM_CAP_BLE && TANDEM_CAP_ESPNOW
static_assert(next_supported_mode(StickTransportMode::UsbHid) == StickTransportMode::BleHid, "S3 next mode from UsbHid must be BleHid");
static_assert(next_supported_mode(StickTransportMode::BleHid) == StickTransportMode::EspNowTx, "S3 next mode from BleHid must be EspNowTx");
static_assert(next_supported_mode(StickTransportMode::EspNowTx) == StickTransportMode::BleConfig, "S3 next mode from EspNowTx must be BleConfig");
static_assert(next_supported_mode(StickTransportMode::BleConfig) == StickTransportMode::UsbHid, "S3 next mode from BleConfig must be UsbHid");
#endif

}  // namespace tandem::stick

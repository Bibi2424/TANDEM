#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/hid_config_transport.h"
#include "tandem/hid_report.h"
#include "tandem/types.h"
#include "tandem_hal/hid_config_endpoint.h"

#ifndef TANDEM_BASE_COMBINED_MODE
#define TANDEM_BASE_COMBINED_MODE 0
#endif

namespace tandem::base_usb {

inline constexpr bool kCombinedMode = (TANDEM_BASE_COMBINED_MODE != 0);
inline constexpr uint16_t kUsbVid = 0x303AU;
inline constexpr uint16_t kUsbPid = kCombinedMode ? 0x80C5U : 0x80C4U;
inline constexpr const char* kHidInterfaceName = kCombinedMode ? "TANDEM Combined Stick" : "TANDEM Base Station";

inline constexpr uint8_t kReportIdLeft = 1U;
inline constexpr uint8_t kReportIdRight = 2U;
inline constexpr size_t kJoystickDescriptorBytes =
    kCombinedMode ? tandem::kHidJoystickReportDescriptorBytes : (tandem::kHidJoystickReportDescriptorBytes * 2U);
inline constexpr size_t kReportDescriptorBytes = kJoystickDescriptorBytes + tandem::config::kHidConfigReportDescriptorBytes;

/// Initializes USB identity and starts the TinyUSB composite HID+CDC device.
void begin() noexcept;

/// Sends a standard 21-byte HID Joystick input report for `report_id`.
bool send_joystick(uint8_t report_id, const tandem::StickState& state) noexcept;

/// Accesses the shared WebHID config endpoint for draining incoming commands.
tandem::hal::HidConfigEndpoint& config_endpoint() noexcept;

template <typename Handler>
void drain_config_commands(Handler&& on_command) noexcept {
  config_endpoint().drain_commands(static_cast<Handler&&>(on_command));
}

bool send_config_response(uint8_t seq, const char* data, size_t len) noexcept;
bool send_config_live(const char* data, size_t len) noexcept;
bool poll_config_tx(uint32_t now_ms, bool& stalled_out) noexcept;

}  // namespace tandem::base_usb

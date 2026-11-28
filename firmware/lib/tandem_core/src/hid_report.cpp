// SPDX-License-Identifier: GPL-3.0-only
#include "tandem/hid_report.h"

namespace tandem {

namespace {

int16_t clamp_axis_sym(int16_t value) noexcept {
  if (value < kAxisMin) {
    return kAxisMin;
  }
  if (value > kAxisMax) {
    return kAxisMax;
  }
  return value;
}

uint8_t sanitize_hat(uint8_t hat) noexcept {
  return (hat <= kHidHatMax) ? hat : kHatCentered;
}

}  // namespace

bool encode_hid_joystick_report(const StickState& state, uint8_t* out, size_t out_len) noexcept {
  if (out == nullptr || out_len < kHidJoystickReportBytes) {
    return false;
  }

  size_t offset = 0;
  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    const uint16_t raw = static_cast<uint16_t>(clamp_axis_sym(state.axes[i]));
    out[offset++] = static_cast<uint8_t>(raw & 0xFFU);
    out[offset++] = static_cast<uint8_t>((raw >> 8U) & 0xFFU);
  }

  const uint32_t buttons = state.buttons;
  out[offset++] = static_cast<uint8_t>(buttons & 0xFFU);
  out[offset++] = static_cast<uint8_t>((buttons >> 8U) & 0xFFU);
  out[offset++] = static_cast<uint8_t>((buttons >> 16U) & 0xFFU);
  out[offset++] = static_cast<uint8_t>((buttons >> 24U) & 0xFFU);

  out[offset++] = static_cast<uint8_t>(sanitize_hat(state.hat) & 0x0FU);
  return true;
}

HidJoystickReport encode_hid_joystick_report(const StickState& state) noexcept {
  HidJoystickReport report{};
  encode_hid_joystick_report(state, report.bytes, sizeof(report.bytes));
  return report;
}

}  // namespace tandem

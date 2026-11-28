// SPDX-License-Identifier: GPL-3.0-only
#include "tandem/stick_combiner.h"

namespace tandem {
namespace {

constexpr uint8_t kMaxValidHat = 8U;

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
  return (hat <= kMaxValidHat) ? hat : kHatCentered;
}

int16_t resolve_axis(const StickState& left, const StickState& right, AxisSource src) noexcept {
  if (src.src_axis >= kMaxAxes) {
    return kAxisCenter;
  }
  switch (src.stick) {
    case StickId::Left:
      return clamp_axis_sym(left.axes[src.src_axis]);
    case StickId::Right:
      return clamp_axis_sym(right.axes[src.src_axis]);
    default:
      return kAxisCenter;
  }
}

bool resolve_button(const StickState& left, const StickState& right, ButtonSource src) noexcept {
  if (src.src_button >= kMaxCombineButtons) {
    return false;
  }
  switch (src.stick) {
    case StickId::Left:
      return ((left.buttons >> src.src_button) & 1U) != 0U;
    case StickId::Right:
      return ((right.buttons >> src.src_button) & 1U) != 0U;
    default:
      return false;
  }
}

uint8_t merge_hats(uint8_t left_hat, uint8_t right_hat, HatMergePolicy policy) noexcept {
  const uint8_t l = sanitize_hat(left_hat);
  const uint8_t r = sanitize_hat(right_hat);
  switch (policy) {
    case HatMergePolicy::LeftThenRight:
      return (l != kHatCentered) ? l : r;
    case HatMergePolicy::RightThenLeft:
      return (r != kHatCentered) ? r : l;
    case HatMergePolicy::LeftOnly:
      return l;
    case HatMergePolicy::RightOnly:
      return r;
    default:
      return kHatCentered;
  }
}

}  // namespace

StickState combine_sticks(const StickState& left, const StickState& right, const StickCombineConfig& config) noexcept {
  StickState out = make_centered_state(kStateFlagNone);

  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    out.axes[i] = resolve_axis(left, right, config.axis_map[i]);
  }

  uint32_t buttons = 0U;
  for (uint8_t i = 0; i < kMaxCombineButtons; ++i) {
    if (resolve_button(left, right, config.button_map[i])) {
      buttons |= (uint32_t{1} << i);
    }
  }
  out.buttons = buttons;

  out.hat = merge_hats(left.hat, right.hat, config.hat_policy);

  uint8_t flags = kStateFlagNone;
  if (((left.flags & kStateFlagConnected) != 0U) || ((right.flags & kStateFlagConnected) != 0U)) {
    flags = static_cast<uint8_t>(flags | kStateFlagConnected);
  }
  if (((left.flags & kStateFlagFailsafe) != 0U) || ((right.flags & kStateFlagFailsafe) != 0U)) {
    flags = static_cast<uint8_t>(flags | kStateFlagFailsafe);
  }
  out.flags = flags;
  return out;
}

}  // namespace tandem

#pragma once

#include <cstdint>
#include <type_traits>

#include "tandem/types.h"

namespace tandem {

inline constexpr uint8_t kMaxCombineButtons = 32U;

// Policy for merging Left and Right 8-way POV hats into a single Combined stick hat.
enum class HatMergePolicy : uint8_t {
  LeftThenRight = 0,
  RightThenLeft = 1,
  LeftOnly = 2,
  RightOnly = 3,
};

// Source selector for a single combined analog axis slot.
// `stick == StickId::Unknown` (or `src_axis >= kMaxAxes`) leaves the output slot centered (0).
struct AxisSource {
  StickId stick{StickId::Unknown};
  uint8_t src_axis{0};
};

// Source selector for a single combined button bit (0..31).
// `stick == StickId::Unknown` (or `src_button >= 32`) leaves the output button released (0).
struct ButtonSource {
  StickId stick{StickId::Unknown};
  uint8_t src_button{0};
};

// Full any-to-any routing table for merging Left and Right StickStates into one StickState.
struct StickCombineConfig {
  AxisSource axis_map[kMaxAxes]{};
  ButtonSource button_map[kMaxCombineButtons]{};
  HatMergePolicy hat_policy{HatMergePolicy::LeftThenRight};
};

static_assert(std::is_trivially_copyable_v<AxisSource>, "AxisSource must be trivially copyable");
static_assert(std::is_trivially_copyable_v<ButtonSource>, "ButtonSource must be trivially copyable");
static_assert(std::is_trivially_copyable_v<StickCombineConfig>, "StickCombineConfig must be trivially copyable");

/// Builds the default HOSAS combined routing:
/// - Axes 0..3 <- Left 0..3, Axes 4..7 <- Right 0..3
/// - Buttons 0..15 <- Left 0..15, Buttons 16..31 <- Right 0..15
/// - Hat <- LeftThenRight
constexpr StickCombineConfig make_default_combine_config() noexcept {
  StickCombineConfig cfg{};
  for (uint8_t i = 0; i < 4U; ++i) {
    cfg.axis_map[i] = AxisSource{StickId::Left, i};
    cfg.axis_map[4U + i] = AxisSource{StickId::Right, i};
  }
  for (uint8_t i = 0; i < 16U; ++i) {
    cfg.button_map[i] = ButtonSource{StickId::Left, i};
    cfg.button_map[16U + i] = ButtonSource{StickId::Right, i};
  }
  cfg.hat_policy = HatMergePolicy::LeftThenRight;
  return cfg;
}

/// Merges post-failsafe `left` and `right` StickStates using `config`.
/// Clamps axes to [kAxisMin, kAxisMax], sanitizes hat > 8 to kHatCentered, and zeroes reserved.
StickState combine_sticks(const StickState& left, const StickState& right, const StickCombineConfig& config = make_default_combine_config()) noexcept;

}  // namespace tandem

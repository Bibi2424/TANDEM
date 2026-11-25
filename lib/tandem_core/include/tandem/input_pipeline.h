#pragma once

#include <cstdint>
#include <type_traits>

#include "tandem/axis_shaping.h"
#include "tandem/types.h"

namespace tandem {

// Raw sampled stick inputs prior to shaping.
struct RawStickInput {
  int32_t axes[kMaxAxes]{};
  uint32_t buttons{0};
  uint8_t hat{kHatCentered};
};

// Per-stick axis configuration array aligned with StickState::axes slots.
struct StickInputConfig {
  AxisConfig axes[kMaxAxes]{};
};

static_assert(std::is_trivially_copyable_v<RawStickInput>, "RawStickInput must be trivially copyable");
static_assert(std::is_trivially_copyable_v<StickInputConfig>, "StickInputConfig must be trivially copyable");
static_assert(std::is_trivially_copyable_v<StickState>, "StickState must be trivially copyable");

/// Shapes all stick axes according to `config` and packs buttons/hat/flags into StickState.
/// Disabled axis slots output kAxisCenter (0) and reserved bytes are zeroed.
StickState process_stick_input(const RawStickInput& raw, const StickInputConfig& config, uint8_t flags = kStateFlagConnected) noexcept;

}  // namespace tandem

#include "tandem/synthetic_input.h"

#include <cmath>
#include <cstdint>

namespace tandem {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPiOver2 = 1.57079632679489661923f;
constexpr uint32_t kAxisPeriodMs = 4000U;
constexpr uint32_t kButtonStepMs = 250U;
constexpr uint32_t kHatStepMs = 500U;

int16_t clamp_axis(long value) noexcept {
  if (value < static_cast<long>(kAxisMin)) {
    return kAxisMin;
  }
  if (value > static_cast<long>(kAxisMax)) {
    return kAxisMax;
  }
  return static_cast<int16_t>(value);
}

}  // namespace

StickState make_synthetic_stick_state(uint32_t now_ms, StickId stick_id) noexcept {
  StickState state = make_centered_state(kStateFlagConnected);

  const float phase =
      (static_cast<float>(now_ms % kAxisPeriodMs) / static_cast<float>(kAxisPeriodMs)) *
      kTwoPi;
  const float stick_offset = (stick_id == StickId::Right) ? kPiOver2 : 0.0f;

  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    const float sample =
        std::sin(phase + stick_offset + static_cast<float>(i) * 0.45f) *
        static_cast<float>(kAxisMax);
    state.axes[i] = clamp_axis(std::lround(sample));
  }

  const uint8_t btn_offset = (stick_id == StickId::Right) ? 16U : 0U;
  const uint8_t btn_idx =
      static_cast<uint8_t>(((now_ms / kButtonStepMs) + btn_offset) % 32U);
  state.buttons = uint32_t{1} << btn_idx;

  state.hat = static_cast<uint8_t>((now_ms / kHatStepMs) % 9U);
  return state;
}

}  // namespace tandem

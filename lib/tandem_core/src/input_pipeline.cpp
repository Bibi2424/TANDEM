#include "tandem/input_pipeline.h"

namespace tandem {

StickState process_stick_input(const RawStickInput& raw,
                               const StickInputConfig& config,
                               uint8_t flags) noexcept {
  StickState state = make_centered_state(flags);
  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    state.axes[i] = shape_axis(raw.axes[i], config.axes[i]);
  }
  state.buttons = raw.buttons;
  state.hat = raw.hat;
  state.reserved[0] = 0;
  state.reserved[1] = 0;
  return state;
}

}  // namespace tandem

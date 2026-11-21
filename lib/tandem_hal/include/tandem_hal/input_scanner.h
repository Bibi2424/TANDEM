#pragma once

#include <cstdint>

#include "tandem/types.h"

namespace tandem {
struct RawStickInput;
}

namespace tandem::hal {

inline constexpr uint8_t kMaxButtons = 32;
inline constexpr uint8_t kHatPinCount = 4;  // N, E, S, W

struct InputScannerConfig {
  // GPIO numbers per analog axis slot; -1 disables the slot.
  int8_t axis_pins[tandem::kMaxAxes]{-1, -1, -1, -1, -1, -1, -1, -1};
  uint8_t adc_bits{12};
  uint8_t adc_oversample{4};

  // Active-low (INPUT_PULLUP) GPIO numbers per button bit; -1 disables the slot.
  int8_t button_pins[kMaxButtons]{
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

  // Optional 4-pin active-low directional hat {N, E, S, W}; -1 disables a direction.
  int8_t hat_pins[kHatPinCount]{-1, -1, -1, -1};

  uint16_t debounce_ms{10};
};

/// Deterministic, zero-allocation ADC1 + GPIO input scanner.
/// Keeps Arduino/ESP-IDF calls isolated from tandem_core.
class InputScanner {
 public:
  void init(const InputScannerConfig& config) noexcept;
  void sample(uint32_t now_ms, tandem::RawStickInput& out) noexcept;

 private:
  struct DebouncedPin {
    int8_t pin{-1};
    bool stable_pressed{false};
    bool last_raw_pressed{false};
    uint32_t last_change_ms{0};

    void init(int8_t gpio_pin, uint32_t now_ms) noexcept;
    bool update(uint32_t now_ms, uint16_t debounce_ms) noexcept;
  };

  InputScannerConfig config_{};
  DebouncedPin buttons_[kMaxButtons]{};
  DebouncedPin hat_[kHatPinCount]{};
  bool initialized_{false};
};

}  // namespace tandem::hal

#pragma once

#include <Arduino.h>

#include <cstdint>

namespace tandem::hal {

enum class ButtonEvent : uint8_t {
  None = 0,
  ShortPress = 1,
  LongPress = 2,
};

/// Debounced active-low BOOT button handler with short- and long-press detection.
class BootButton {
 public:
  static constexpr uint32_t kDefaultDebounceMs = 30U;
  static constexpr uint32_t kDefaultLongPressMs = 1000U;

  void init(uint8_t pin, uint32_t debounce_ms = kDefaultDebounceMs, uint32_t long_press_ms = kDefaultLongPressMs) noexcept {
    pin_ = pin;
    debounce_ms_ = debounce_ms;
    long_press_ms_ = long_press_ms;
    pinMode(pin_, INPUT_PULLUP);
    stable_pressed_ = false;
    last_raw_pressed_ = false;
    last_change_ms_ = millis();
    press_start_ms_ = last_change_ms_;
    long_fired_ = false;
  }

  ButtonEvent poll(uint32_t now_ms) noexcept {
    ButtonEvent ev = ButtonEvent::None;
    const bool raw_pressed = (digitalRead(pin_) == LOW);
    if (raw_pressed != last_raw_pressed_) {
      last_raw_pressed_ = raw_pressed;
      last_change_ms_ = now_ms;
    } else if (raw_pressed != stable_pressed_ && static_cast<uint32_t>(now_ms - last_change_ms_) >= debounce_ms_) {
      stable_pressed_ = raw_pressed;
      if (stable_pressed_) {
        press_start_ms_ = now_ms;
        long_fired_ = false;
      } else if (!long_fired_) {
        ev = ButtonEvent::ShortPress;
      }
    }

    if (stable_pressed_ && !long_fired_ && static_cast<uint32_t>(now_ms - press_start_ms_) >= long_press_ms_) {
      long_fired_ = true;
      ev = ButtonEvent::LongPress;
    }
    return ev;
  }

 private:
  uint8_t pin_{0};
  uint32_t debounce_ms_{kDefaultDebounceMs};
  uint32_t long_press_ms_{kDefaultLongPressMs};
  bool stable_pressed_{false};
  bool last_raw_pressed_{false};
  uint32_t last_change_ms_{0};
  uint32_t press_start_ms_{0};
  bool long_fired_{false};
};

}  // namespace tandem::hal

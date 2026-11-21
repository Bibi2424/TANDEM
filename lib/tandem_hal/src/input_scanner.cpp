#include "tandem_hal/input_scanner.h"

#include <Arduino.h>

#include "tandem/input_pipeline.h"

namespace tandem::hal {

namespace {

constexpr uint8_t kMinOversample = 1;
constexpr uint8_t kMaxOversample = 16;

// Maps (dy + 1, dx + 1) for dy = N - S in {-1, 0, +1}, dx = E - W in {-1, 0, +1}
// to canonical 8-way POV encoding: 0 = centered, 1 = N .. 8 = NW.
constexpr uint8_t kHatLookup[3][3] = {
    {6, 5, 4},  // dy = -1 (S):  SW, S, SE
    {7, 0, 3},  // dy =  0:      W,  C, E
    {8, 1, 2},  // dy = +1 (N):  NW, N, NE
};

uint8_t clamp_oversample(uint8_t count) noexcept {
  if (count < kMinOversample) {
    return kMinOversample;
  }
  if (count > kMaxOversample) {
    return kMaxOversample;
  }
  return count;
}

uint8_t encode_hat_4way(bool north, bool east, bool south, bool west) noexcept {
  const int dy = (north ? 1 : 0) - (south ? 1 : 0);
  const int dx = (east ? 1 : 0) - (west ? 1 : 0);
  return kHatLookup[dy + 1][dx + 1];
}

}  // namespace

void InputScanner::DebouncedPin::init(int8_t gpio_pin, uint32_t now_ms) noexcept {
  pin = gpio_pin;
  if (pin < 0) {
    stable_pressed = false;
    last_raw_pressed = false;
    last_change_ms = now_ms;
    return;
  }
  pinMode(static_cast<uint8_t>(pin), INPUT_PULLUP);
  const bool pressed = (digitalRead(static_cast<uint8_t>(pin)) == LOW);
  stable_pressed = pressed;
  last_raw_pressed = pressed;
  last_change_ms = now_ms;
}

bool InputScanner::DebouncedPin::update(uint32_t now_ms, uint16_t debounce_ms) noexcept {
  if (pin < 0) {
    return false;
  }
  const bool raw_pressed = (digitalRead(static_cast<uint8_t>(pin)) == LOW);
  if (raw_pressed != last_raw_pressed) {
    last_raw_pressed = raw_pressed;
    last_change_ms = now_ms;
  } else if (raw_pressed != stable_pressed &&
             static_cast<uint32_t>(now_ms - last_change_ms) >= debounce_ms) {
    stable_pressed = raw_pressed;
  }
  return stable_pressed;
}

void InputScanner::init(const InputScannerConfig& config) noexcept {
  config_ = config;
  config_.adc_oversample = clamp_oversample(config_.adc_oversample);

  analogReadResolution(config_.adc_bits);
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    const int8_t pin = config_.axis_pins[i];
    if (pin >= 0) {
      analogSetPinAttenuation(static_cast<uint8_t>(pin), ADC_11db);
    }
  }

  const uint32_t now_ms = millis();
  for (uint8_t i = 0; i < kMaxButtons; ++i) {
    buttons_[i].init(config_.button_pins[i], now_ms);
  }
  for (uint8_t i = 0; i < kHatPinCount; ++i) {
    hat_[i].init(config_.hat_pins[i], now_ms);
  }

  initialized_ = true;
}

void InputScanner::sample(uint32_t now_ms, tandem::RawStickInput& out) noexcept {
  out = tandem::RawStickInput{};
  if (!initialized_) {
    return;
  }

  const uint8_t samples = config_.adc_oversample;
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    const int8_t pin = config_.axis_pins[i];
    if (pin < 0) {
      out.axes[i] = 0;
      continue;
    }
    int32_t sum = 0;
    for (uint8_t s = 0; s < samples; ++s) {
      sum += analogRead(static_cast<uint8_t>(pin));
    }
    out.axes[i] = sum / static_cast<int32_t>(samples);
  }

  uint32_t button_mask = 0;
  for (uint8_t i = 0; i < kMaxButtons; ++i) {
    if (buttons_[i].update(now_ms, config_.debounce_ms)) {
      button_mask |= (uint32_t{1} << i);
    }
  }
  out.buttons = button_mask;

  const bool north = hat_[0].update(now_ms, config_.debounce_ms);
  const bool east = hat_[1].update(now_ms, config_.debounce_ms);
  const bool south = hat_[2].update(now_ms, config_.debounce_ms);
  const bool west = hat_[3].update(now_ms, config_.debounce_ms);
  out.hat = encode_hat_4way(north, east, south, west);
}

}  // namespace tandem::hal

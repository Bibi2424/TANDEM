#pragma once

#include <cstdint>

#include "tandem_hal/input_scanner.h"

// WEMOS LOLIN S2 Mini (lolin_s2_mini) prototyping board capabilities & pinmap.
// - Wireless: Wi-Fi/ESP-NOW (no BLE)
// - USB: Native USB OTG on GPIO19 (D-) / GPIO20 (D+) — kept reserved
//        lolin_s2_mini provides -DARDUINO_USB_CDC_ON_BOOT=1, -DARDUINO_USB_MODE=0
// - Analog: Prefer ADC1 (GPIO1-GPIO10); hardware supports up to 13-bit, normalized to 12-bit here
// - Avoid strapping pins (GPIO0, GPIO45, GPIO46).

namespace tandem::board {

inline constexpr const char* kBoardName = "ESP32-S2 (LOLIN S2 Mini)";
inline constexpr bool kHasNativeUsb = true;
inline constexpr bool kHasBle = false;
inline constexpr bool kHasEspNow = true;
inline constexpr uint8_t kAdcBits = 12;

// Onboard status LED on LOLIN S2 Mini (GPIO15); -1 if absent.
inline constexpr int8_t kPinStatusLed = 15;

// Tentative ADC1 axis pins — confirm against physical dev-board breakout before use.
inline constexpr uint8_t kPinAxisX = 1;
inline constexpr uint8_t kPinAxisY = 2;
inline constexpr uint8_t kPinAxisZ = 3;
inline constexpr uint8_t kPinAxisThrottle = 4;

// Safe digital GPIO button pins (active-low INPUT_PULLUP; avoids USB 19/20, LED 15, strapping 0/45/46).
inline constexpr uint8_t kPinButton0 = 5;
inline constexpr uint8_t kPinButton1 = 6;
inline constexpr uint8_t kPinButton2 = 7;
inline constexpr uint8_t kPinButton3 = 8;

inline tandem::hal::InputScannerConfig make_default_scanner_config() noexcept {
  tandem::hal::InputScannerConfig cfg{};
  cfg.adc_bits = kAdcBits;
  cfg.axis_pins[0] = static_cast<int8_t>(kPinAxisX);
  cfg.axis_pins[1] = static_cast<int8_t>(kPinAxisY);
  cfg.axis_pins[2] = static_cast<int8_t>(kPinAxisZ);
  cfg.axis_pins[3] = static_cast<int8_t>(kPinAxisThrottle);
  cfg.button_pins[0] = static_cast<int8_t>(kPinButton0);
  cfg.button_pins[1] = static_cast<int8_t>(kPinButton1);
  cfg.button_pins[2] = static_cast<int8_t>(kPinButton2);
  cfg.button_pins[3] = static_cast<int8_t>(kPinButton3);
  return cfg;
}

}  // namespace tandem::board

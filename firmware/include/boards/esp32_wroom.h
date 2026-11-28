// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

#include "tandem_hal/input_scanner.h"

// Classic ESP32 (ESP32-WROOM-32) prototyping board capabilities & placeholder pins.
// - Wireless: BLE + Wi-Fi/ESP-NOW
// - USB: No native USB (uses external USB-UART bridge)
// - Analog: ADC1 only (GPIO32-GPIO39; GPIO34-GPIO39 are input-only with no internal pull-ups)
// - Avoid SPI flash pins (GPIO6-GPIO11) and strapping pins (GPIO0, GPIO2, GPIO5, GPIO12, GPIO15).

#define TANDEM_CAP_NATIVE_USB 0
#define TANDEM_CAP_BLE 1
#define TANDEM_CAP_ESPNOW 1

namespace tandem::board {

inline constexpr const char* kBoardName = "ESP32-WROOM";
inline constexpr bool kHasNativeUsb = (TANDEM_CAP_NATIVE_USB != 0);
inline constexpr bool kHasBle = (TANDEM_CAP_BLE != 0);
inline constexpr bool kHasEspNow = (TANDEM_CAP_ESPNOW != 0);
inline constexpr uint8_t kAdcBits = 12;

// Onboard status LED on classic ESP32 devkits (typically GPIO2); -1 if absent.
inline constexpr int8_t kPinStatusLed = 2;
inline constexpr uint8_t kPinBootButton = 0;  // Active-low BOOT/GPIO0 button

// Tentative ADC1 axis pins — confirm against physical wiring before use.
inline constexpr uint8_t kPinAxisX = 34;
inline constexpr uint8_t kPinAxisY = 35;
inline constexpr uint8_t kPinAxisZ = 32;
inline constexpr uint8_t kPinAxisThrottle = 33;

// Safe digital GPIO button pins with internal pull-ups (avoids 34-39 input-only, 6-11 flash, 0/2/5/12/15 strapping).
inline constexpr uint8_t kPinButton0 = 18;
inline constexpr uint8_t kPinButton1 = 19;
inline constexpr uint8_t kPinButton2 = 21;
inline constexpr uint8_t kPinButton3 = 22;

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

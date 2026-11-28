// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

#include "tandem_hal/input_scanner.h"

// ESP32-S3 candidate board capabilities & placeholder pins.
// - Wireless: BLE + Wi-Fi/ESP-NOW
// - USB: Native USB on GPIO19 (D-) / GPIO20 (D+) — kept reserved
// - Analog: Prefer ADC1 (GPIO1-GPIO10), avoiding strapping pin GPIO3
// - Avoid strapping pins (GPIO0, GPIO3, GPIO45, GPIO46).

#define TANDEM_CAP_NATIVE_USB 1
#define TANDEM_CAP_BLE 1
#define TANDEM_CAP_ESPNOW 1

namespace tandem::board {

inline constexpr const char* kBoardName = "ESP32-S3";
inline constexpr bool kHasNativeUsb = (TANDEM_CAP_NATIVE_USB != 0);
inline constexpr bool kHasBle = (TANDEM_CAP_BLE != 0);
inline constexpr bool kHasEspNow = (TANDEM_CAP_ESPNOW != 0);
inline constexpr uint8_t kAdcBits = 12;

// Tentative ADC1 axis pins — confirm against physical dev-board breakout before use.
inline constexpr uint8_t kPinAxisX = 1;
inline constexpr uint8_t kPinAxisY = 2;
inline constexpr uint8_t kPinAxisZ = 4;
inline constexpr uint8_t kPinAxisThrottle = 5;

// Status LED varies by ESP32-S3 devkit (e.g. addressable RGB vs none); disabled by default.
inline constexpr int8_t kPinStatusLed = -1;
inline constexpr uint8_t kPinBootButton = 0;  // Active-low BOOT/GPIO0 button

// Tentative active-low button pins (with internal pull-ups).
inline constexpr uint8_t kPinButton0 = 6;
inline constexpr uint8_t kPinButton1 = 7;
inline constexpr uint8_t kPinButton2 = 8;
inline constexpr uint8_t kPinButton3 = 9;

inline tandem::hal::InputScannerConfig make_default_scanner_config() noexcept {
  tandem::hal::InputScannerConfig cfg{};
  cfg.axis_pins[0] = kPinAxisX;
  cfg.axis_pins[1] = kPinAxisY;
  cfg.axis_pins[2] = kPinAxisZ;
  cfg.axis_pins[3] = kPinAxisThrottle;
  cfg.adc_bits = kAdcBits;
  cfg.adc_oversample = 4;
  cfg.button_pins[0] = kPinButton0;
  cfg.button_pins[1] = kPinButton1;
  cfg.button_pins[2] = kPinButton2;
  cfg.button_pins[3] = kPinButton3;
  cfg.debounce_ms = 10;
  return cfg;
}

}  // namespace tandem::board

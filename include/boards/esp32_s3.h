#pragma once

#include <cstdint>

// ESP32-S3 candidate board capabilities & placeholder pins.
// - Wireless: BLE + Wi-Fi/ESP-NOW
// - USB: Native USB on GPIO19 (D-) / GPIO20 (D+) — kept reserved
// - Analog: Prefer ADC1 (GPIO1-GPIO10), avoiding strapping pin GPIO3
// - Avoid strapping pins (GPIO0, GPIO3, GPIO45, GPIO46).

namespace tandem::board {

inline constexpr const char* kBoardName = "ESP32-S3";
inline constexpr bool kHasNativeUsb = true;
inline constexpr bool kHasBle = true;
inline constexpr bool kHasEspNow = true;
inline constexpr uint8_t kAdcBits = 12;

// Tentative ADC1 axis pins — confirm against physical dev-board breakout before use.
inline constexpr uint8_t kPinAxisX = 1;
inline constexpr uint8_t kPinAxisY = 2;
inline constexpr uint8_t kPinAxisZ = 4;
inline constexpr uint8_t kPinAxisThrottle = 5;

}  // namespace tandem::board

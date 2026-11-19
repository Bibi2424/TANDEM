#pragma once

#include <cstdint>

// ESP32-S2 prototyping board capabilities & placeholder pins.
// - Wireless: Wi-Fi/ESP-NOW (no BLE)
// - USB: Native USB OTG on GPIO19 (D-) / GPIO20 (D+) — kept reserved
// - Analog: Prefer ADC1 (GPIO1-GPIO10); hardware supports up to 13-bit, normalized to 12-bit here
// - Avoid strapping pins (GPIO0, GPIO45, GPIO46).

namespace tandem::board {

inline constexpr const char* kBoardName = "ESP32-S2";
inline constexpr bool kHasNativeUsb = true;
inline constexpr bool kHasBle = false;
inline constexpr bool kHasEspNow = true;
inline constexpr uint8_t kAdcBits = 12;

// Tentative ADC1 axis pins — confirm against physical dev-board breakout before use.
inline constexpr uint8_t kPinAxisX = 1;
inline constexpr uint8_t kPinAxisY = 2;
inline constexpr uint8_t kPinAxisZ = 3;
inline constexpr uint8_t kPinAxisThrottle = 4;

}  // namespace tandem::board

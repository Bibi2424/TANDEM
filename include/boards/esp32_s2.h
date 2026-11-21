#pragma once

#include <cstdint>

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

}  // namespace tandem::board

#pragma once

#include <cstdint>

// Classic ESP32 (ESP32-WROOM-32) prototyping board capabilities & placeholder pins.
// - Wireless: BLE + Wi-Fi/ESP-NOW
// - USB: No native USB (uses external USB-UART bridge)
// - Analog: ADC1 only (GPIO32-GPIO39; GPIO34-GPIO39 are input-only with no internal pull-ups)
// - Avoid SPI flash pins (GPIO6-GPIO11) and strapping pins (GPIO0, GPIO2, GPIO5, GPIO12, GPIO15).

namespace tandem::board {

inline constexpr const char* kBoardName = "ESP32-WROOM";
inline constexpr bool kHasNativeUsb = false;
inline constexpr bool kHasBle = true;
inline constexpr bool kHasEspNow = true;
inline constexpr uint8_t kAdcBits = 12;

// Tentative ADC1 axis pins — confirm against physical wiring before use.
inline constexpr uint8_t kPinAxisX = 34;
inline constexpr uint8_t kPinAxisY = 35;
inline constexpr uint8_t kPinAxisZ = 32;
inline constexpr uint8_t kPinAxisThrottle = 33;

}  // namespace tandem::board

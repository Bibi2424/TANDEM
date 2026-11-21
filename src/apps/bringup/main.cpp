#include <Arduino.h>
#include <cstdint>

#include "boards/board_config.h"
#include "tandem/core_info.h"
#include "tandem/types.h"

namespace {

constexpr uint32_t kHeartbeatIntervalMs = 2000;
constexpr uint32_t kUsbSerialWaitMs = 1500;
uint32_t g_last_heartbeat_ms = 0;
bool g_led_state = false;

}  // namespace

void setup() {
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    pinMode(tandem::board::kPinStatusLed, OUTPUT);
    digitalWrite(tandem::board::kPinStatusLed, LOW);
  }

  Serial.begin(115200);
  // Bounded wait so native USB CDC has time to enumerate without blocking headless boots.
  const uint32_t wait_start_ms = millis();
  while (!Serial && static_cast<uint32_t>(millis() - wait_start_ms) < kUsbSerialWaitMs) {
    delay(10);
  }
  delay(100);

  const tandem::StickState centered =
      tandem::make_centered_state(tandem::kStateFlagFailsafe);

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. Bring-Up ===");
  Serial.printf("Core version : %s\n", tandem::core_version());
  Serial.printf("Board        : %s\n", tandem::board::kBoardName);
  Serial.printf("Capabilities : USB=%d BLE=%d ESP-NOW=%d ADC=%u-bit LED=%d\n",
                tandem::board::kHasNativeUsb ? 1 : 0,
                tandem::board::kHasBle ? 1 : 0,
                tandem::board::kHasEspNow ? 1 : 0,
                static_cast<unsigned>(tandem::board::kAdcBits),
                static_cast<int>(tandem::board::kPinStatusLed));
  Serial.printf("StickState   : %u bytes (flags=0x%02X)\n",
                static_cast<unsigned>(sizeof(centered)),
                static_cast<unsigned>(centered.flags));

  g_last_heartbeat_ms = millis();
}

void loop() {
  const uint32_t now_ms = millis();
  if (static_cast<uint32_t>(now_ms - g_last_heartbeat_ms) >= kHeartbeatIntervalMs) {
    g_last_heartbeat_ms = now_ms;
    if constexpr (tandem::board::kPinStatusLed >= 0) {
      g_led_state = !g_led_state;
      digitalWrite(tandem::board::kPinStatusLed, g_led_state ? HIGH : LOW);
    }
    Serial.printf("[%lu ms] %s alive\n",
                  static_cast<unsigned long>(now_ms),
                  tandem::board::kBoardName);
  }
}

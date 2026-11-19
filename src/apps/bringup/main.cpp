#include <Arduino.h>
#include <cstdint>

#include "boards/board_config.h"
#include "tandem/core_info.h"
#include "tandem/types.h"

namespace {

constexpr uint32_t kHeartbeatIntervalMs = 2000;
uint32_t g_last_heartbeat_ms = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  const tandem::StickState centered =
      tandem::make_centered_state(tandem::kStateFlagFailsafe);

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. Bring-Up ===");
  Serial.printf("Core version : %s\n", tandem::core_version());
  Serial.printf("Board        : %s\n", tandem::board::kBoardName);
  Serial.printf("Capabilities : USB=%d BLE=%d ESP-NOW=%d ADC=%u-bit\n",
                tandem::board::kHasNativeUsb ? 1 : 0,
                tandem::board::kHasBle ? 1 : 0,
                tandem::board::kHasEspNow ? 1 : 0,
                static_cast<unsigned>(tandem::board::kAdcBits));
  Serial.printf("StickState   : %u bytes (flags=0x%02X)\n",
                static_cast<unsigned>(sizeof(centered)),
                static_cast<unsigned>(centered.flags));

  g_last_heartbeat_ms = millis();
}

void loop() {
  const uint32_t now_ms = millis();
  if (static_cast<uint32_t>(now_ms - g_last_heartbeat_ms) >= kHeartbeatIntervalMs) {
    g_last_heartbeat_ms = now_ms;
    Serial.printf("[%lu ms] %s alive\n",
                  static_cast<unsigned long>(now_ms),
                  tandem::board::kBoardName);
  }
}

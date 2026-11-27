#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "boards/board_config.h"
#include "stick_transport.h"
#include "tandem/hid_report.h"

#if TANDEM_CAP_BLE

namespace tandem::stick {

class BleHidTransport final : public StickTransport {
 public:
  uint32_t interval_us() const noexcept override { return 20000; }  // 50 Hz BLE HID (dual-stick safe)
  bool is_active() const noexcept override { return active_; }
  bool start() noexcept override;
  void stop() noexcept override;
  void poll() noexcept override;
  void fill_telemetry(tandem::config::TelemetrySnapshot& out) const noexcept override;
  bool send(tandem::StickId stick_id, const tandem::StickState& state) noexcept override;
  void format_stats(char* out, size_t out_len) const noexcept override;
  const char* device_name() const noexcept override { return (device_name_[0] != '\0') ? device_name_ : nullptr; }

  bool connected() const noexcept;

  static void on_connect_cb() noexcept;
  static void on_disconnect_cb() noexcept;

 private:
  bool init_ble_stack() noexcept;

  static std::atomic<bool> ble_connected_;
  static std::atomic<bool> ble_restart_adv_;

  bool stack_initialized_{false};
  bool active_{false};
  uint32_t sent_{0};
  tandem::HidJoystickReport last_report_{};
  bool has_last_report_{false};
  int64_t last_notify_us_{0};
  char device_name_[32]{};
};

}  // namespace tandem::stick

#endif  // TANDEM_CAP_BLE

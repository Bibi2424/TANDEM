#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "boards/board_config.h"
#include "stick_transport.h"

#if TANDEM_CAP_BLE

namespace tandem::stick {

class BleHidTransport final : public StickTransport {
 public:
  uint32_t interval_us() const noexcept override { return 10000; }  // 100 Hz BLE HID
  bool is_active() const noexcept override { return active_; }
  bool start() noexcept override;
  void stop() noexcept override;
  void poll() noexcept override;
  bool send(tandem::StickId stick_id,
            const tandem::StickState& state) noexcept override;
  void format_stats(char* out, size_t out_len) const noexcept override;

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
};

}  // namespace tandem::stick

#endif  // TANDEM_CAP_BLE

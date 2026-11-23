#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "boards/board_config.h"
#include "stick_transport.h"
#include "tandem/config_protocol.h"

#if TANDEM_CAP_BLE

namespace tandem::stick {

class BleConfigTransport final : public StickTransport {
 public:
  uint32_t interval_us() const noexcept override { return 10000; }  // 100 Hz input poll
  bool is_active() const noexcept override { return active_; }
  bool start() noexcept override;
  void stop() noexcept override;
  void poll() noexcept override;
  bool send(tandem::StickId stick_id,
            const tandem::StickState& state) noexcept override;
  void format_stats(char* out, size_t out_len) const noexcept override;

  bool connected() const noexcept;

  /// Pops the next queued command line received via BLE GATT Write.
  /// Returns true if a line was popped (or `overflow` is set if a line exceeded max length).
  bool pop_command(char* out, size_t out_size, bool& overflow) noexcept;

  /// Sends a `@TANDEM {...}\n` response frame over the Response Notify characteristic.
  bool send_response(const char* data, size_t len) noexcept;

  /// Sends a `@TANDEM {"v":1,"cmd":"live",...}\n` frame over the Live Notify characteristic.
  bool send_live(const char* data, size_t len) noexcept;

  static void on_connect_cb() noexcept;
  static void on_disconnect_cb() noexcept;
  static void on_cmd_write_cb(const uint8_t* data, size_t len) noexcept;

 private:
  bool init_ble_stack() noexcept;

  static std::atomic<bool> ble_connected_;
  static std::atomic<bool> ble_restart_adv_;

  bool stack_initialized_{false};
  bool active_{false};
  uint32_t rx_cmds_{0};
  uint32_t tx_frames_{0};
};

}  // namespace tandem::stick

#endif  // TANDEM_CAP_BLE

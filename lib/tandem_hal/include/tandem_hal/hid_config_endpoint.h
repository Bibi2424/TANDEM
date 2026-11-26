#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "tandem/config_protocol.h"
#include "tandem/hid_config_transport.h"

namespace tandem::hal {

/// Thread-safe ESP32 adapter bridging HID callback RX chunks and main-loop TX polling
/// with `tandem::config::FixedHidConfigReassembler` and `HidConfigTxQueue`.
class HidConfigEndpoint {
 public:
  static constexpr size_t kCommandQueueDepth = 4U;
  static constexpr uint32_t kTxStallTimeoutMs = 500U;

  using SendChunkFn = bool (*)(void* ctx, const uint8_t* data, size_t len) noexcept;

  explicit HidConfigEndpoint(size_t report_bytes = tandem::config::kHidConfigUsbReportBytes) noexcept;

  /// ISR/task-safe handler called when one HID Output/Feature report payload arrives.
  void on_rx_chunk(const uint8_t* data, size_t len) noexcept;

  /// Pops one queued complete command line (or overflow notification) from the ring buffer.
  bool pop_command(char* out, size_t out_size, uint8_t& seq_out, bool& overflow_out) noexcept;

  /// Drains all queued commands; automatically replies `line_too_long` on overflow.
  template <typename Handler>
  void drain_commands(Handler&& on_command) noexcept {
    char cmd[tandem::config::kMaxCommandLength]{};
    uint8_t seq = 0U;
    bool overflow = false;
    while (pop_command(cmd, sizeof(cmd), seq, overflow)) {
      if (overflow) {
        char err_resp[128]{};
        const int n = std::snprintf(err_resp, sizeof(err_resp), "%s{\"v\":%u,\"cmd\":\"unknown\",\"ok\":false,\"err\":\"line_too_long\"}\n",
                                    tandem::config::kResponsePrefix, static_cast<unsigned>(tandem::config::kProtocolVersion));
        if (n > 0) {
          enqueue_response(seq, err_resp, static_cast<size_t>(n));
        }
      } else {
        on_command(static_cast<const char*>(cmd), seq);
      }
    }
  }

  bool enqueue_response(uint8_t seq, const char* data, size_t len) noexcept;
  bool enqueue_live(const char* data, size_t len) noexcept;

  /// Sends at most one pending TX chunk; resets TX queue and sets `stalled_out` on timeout.
  bool poll_tx(uint32_t now_ms, SendChunkFn send_fn, void* ctx, bool& stalled_out) noexcept;
  bool has_tx_pending() const noexcept { return tx_queue_.has_pending(); }
  void reset_tx() noexcept;

 private:
  struct QueuedCommand {
    char line[tandem::config::kMaxCommandLength]{};
    uint8_t seq{0};
    bool overflow{false};
  };

  void enqueue_command_locked(const char* line, uint8_t seq, bool overflow) noexcept;

  portMUX_TYPE rx_mux_ = portMUX_INITIALIZER_UNLOCKED;
  tandem::config::FixedHidConfigReassembler<tandem::config::kMaxCommandLength> reassembler_{};
  QueuedCommand cmd_queue_[kCommandQueueDepth]{};
  size_t cmd_head_{0U};
  size_t cmd_tail_{0U};
  size_t cmd_count_{0U};

  tandem::config::HidConfigTxQueue tx_queue_;
  uint32_t tx_pending_since_ms_{0U};
  bool tx_pending_since_valid_{false};
};

}  // namespace tandem::hal

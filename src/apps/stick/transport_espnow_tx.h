#pragma once

#include <cstddef>
#include <cstdint>

#include "boards/board_config.h"
#include "stick_transport.h"

#if TANDEM_CAP_ESPNOW

#ifndef TANDEM_ESPNOW_TX_HZ
#define TANDEM_ESPNOW_TX_HZ 250
#endif

static_assert(TANDEM_ESPNOW_TX_HZ > 0 && TANDEM_ESPNOW_TX_HZ <= 1000,
              "TANDEM_ESPNOW_TX_HZ must be in [1, 1000]");

namespace tandem::stick {

class EspNowTxTransport final : public StickTransport {
 public:
  void configure(uint8_t channel, const uint8_t peer_mac[6]) noexcept;

  uint32_t interval_us() const noexcept override {
    return 1000000UL / static_cast<uint32_t>(TANDEM_ESPNOW_TX_HZ);
  }
  bool is_active() const noexcept override { return active_; }
  bool start() noexcept override;
  void stop() noexcept override;
  void on_stick_id_changed() noexcept override { seq_ = 0; }
  bool send(tandem::StickId stick_id,
            const tandem::StickState& state) noexcept override;
  void format_stats(char* out, size_t out_len) const noexcept override;

 private:
  bool active_{false};
  uint8_t channel_{1};
  uint8_t peer_mac_[6]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint16_t seq_{0};
  uint32_t tx_queued_{0};
  uint32_t tx_errors_{0};
};

}  // namespace tandem::stick

#endif  // TANDEM_CAP_ESPNOW

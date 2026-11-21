#pragma once

#include <cstdint>

#include "tandem/types.h"

namespace tandem {

// Wireless/transport link status tracked by FailsafeMonitor.
enum class LinkStatus : uint8_t {
  Disconnected = 0,
  Connected = 1,
  TimedOut = 2,
};

// Link-loss failsafe configuration.
struct FailsafeConfig {
  uint32_t timeout_ms{200};
  StickState failsafe_state{make_centered_state(kStateFlagFailsafe)};
};

/// Returns true when `config` has a non-zero link timeout.
constexpr bool is_valid(const FailsafeConfig& config) noexcept {
  return config.timeout_ms > 0U;
}

/// Unsigned-wraparound-safe, zero-allocation link-loss failsafe monitor.
/// Starts fail-closed (Disconnected) and latches TimedOut on expiration so a
/// 49.7-day uint32_t timer wraparound cannot resurrect stale inputs.
class FailsafeMonitor {
 public:
  FailsafeMonitor() noexcept;
  explicit FailsafeMonitor(const FailsafeConfig& config) noexcept;

  /// Records a freshly received StickState at `now_ms`, sanitizing bounds/flags.
  /// Remains fail-closed if `config.timeout_ms == 0`.
  void on_packet(const StickState& state, uint32_t now_ms) noexcept;

  /// Evaluates link timeout at `now_ms` using unsigned subtraction.
  void update(uint32_t now_ms) noexcept;

  /// Immediately enters failsafe (e.g., on explicit BLE/radio disconnect).
  void trigger_failsafe() noexcept;

  /// Resets to the initial Disconnected failsafe state.
  void reset() noexcept;

  /// Updates configuration and enforces fail-closed state if timeout_ms == 0.
  void set_config(const FailsafeConfig& config) noexcept;

  const FailsafeConfig& config() const noexcept { return config_; }
  const StickState& state() const noexcept { return active_state_; }
  LinkStatus status() const noexcept { return status_; }
  bool is_connected() const noexcept { return status_ == LinkStatus::Connected; }
  bool is_failsafe() const noexcept { return status_ != LinkStatus::Connected; }
  bool has_received_packet() const noexcept { return has_packet_; }
  uint32_t last_packet_ms() const noexcept { return last_packet_ms_; }

 private:
  FailsafeConfig config_{};
  StickState sanitized_failsafe_state_{make_centered_state(kStateFlagFailsafe)};
  StickState active_state_{make_centered_state(kStateFlagFailsafe)};
  uint32_t last_packet_ms_{0};
  LinkStatus status_{LinkStatus::Disconnected};
  bool has_packet_{false};
};

}  // namespace tandem

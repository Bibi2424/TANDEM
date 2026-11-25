#include "tandem/failsafe.h"

namespace tandem {
namespace {

int16_t clamp_axis_sym(int16_t value) noexcept {
  if (value < kAxisMin) {
    return kAxisMin;
  }
  if (value > kAxisMax) {
    return kAxisMax;
  }
  return value;
}

StickState sanitize_state(const StickState& in, uint8_t set_flag, uint8_t clear_flag) noexcept {
  StickState out = in;
  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    out.axes[i] = clamp_axis_sym(out.axes[i]);
  }
  out.flags = static_cast<uint8_t>((out.flags & ~clear_flag) | set_flag);
  out.reserved[0] = 0;
  out.reserved[1] = 0;
  return out;
}

}  // namespace

FailsafeMonitor::FailsafeMonitor() noexcept : FailsafeMonitor(FailsafeConfig{}) {}

FailsafeMonitor::FailsafeMonitor(const FailsafeConfig& config) noexcept {
  set_config(config);
}

void FailsafeMonitor::on_packet(const StickState& state, uint32_t now_ms) noexcept {
  last_packet_ms_ = now_ms;
  has_packet_ = true;

  if (!is_valid(config_)) {
    status_ = LinkStatus::TimedOut;
    active_state_ = sanitized_failsafe_state_;
    return;
  }

  active_state_ = sanitize_state(state, kStateFlagConnected, kStateFlagFailsafe);
  status_ = LinkStatus::Connected;
}

void FailsafeMonitor::update(uint32_t now_ms) noexcept {
  if (status_ != LinkStatus::Connected) {
    return;
  }

  if (!is_valid(config_) || static_cast<uint32_t>(now_ms - last_packet_ms_) >= config_.timeout_ms) {
    status_ = LinkStatus::TimedOut;
    active_state_ = sanitized_failsafe_state_;
  }
}

void FailsafeMonitor::trigger_failsafe() noexcept {
  status_ = has_packet_ ? LinkStatus::TimedOut : LinkStatus::Disconnected;
  active_state_ = sanitized_failsafe_state_;
}

void FailsafeMonitor::reset() noexcept {
  status_ = LinkStatus::Disconnected;
  has_packet_ = false;
  last_packet_ms_ = 0;
  active_state_ = sanitized_failsafe_state_;
}

void FailsafeMonitor::set_config(const FailsafeConfig& config) noexcept {
  config_ = config;
  sanitized_failsafe_state_ = sanitize_state(config_.failsafe_state, kStateFlagFailsafe, kStateFlagConnected);
  config_.failsafe_state = sanitized_failsafe_state_;

  if (status_ != LinkStatus::Connected || !is_valid(config_)) {
    if (status_ == LinkStatus::Connected && !is_valid(config_)) {
      status_ = LinkStatus::TimedOut;
    }
    active_state_ = sanitized_failsafe_state_;
  }
}

}  // namespace tandem

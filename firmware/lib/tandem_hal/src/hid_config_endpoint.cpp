// SPDX-License-Identifier: GPL-3.0-only
#include "tandem_hal/hid_config_endpoint.h"

#include <cstring>

namespace tandem::hal {

HidConfigEndpoint::HidConfigEndpoint(size_t report_bytes) noexcept : tx_queue_(report_bytes) {}

void HidConfigEndpoint::enqueue_command_locked(const char* line, uint8_t seq, bool overflow) noexcept {
  if (cmd_count_ >= kCommandQueueDepth) {
    return;
  }
  QueuedCommand& slot = cmd_queue_[cmd_tail_];
  slot.seq = seq;
  slot.overflow = overflow;
  if (!overflow && line != nullptr) {
    std::strncpy(slot.line, line, sizeof(slot.line) - 1U);
    slot.line[sizeof(slot.line) - 1U] = '\0';
  } else {
    slot.line[0] = '\0';
  }
  cmd_tail_ = (cmd_tail_ + 1U) % kCommandQueueDepth;
  ++cmd_count_;
}

void HidConfigEndpoint::on_rx_chunk(const uint8_t* data, size_t len) noexcept {
  if (data == nullptr || len == 0U) {
    return;
  }
  tandem::config::HidConfigMessageView view{};
  portENTER_CRITICAL(&rx_mux_);
  const auto res = reassembler_.push_chunk(data, len, view);
  if (res == tandem::config::HidConfigReassembler::FeedResult::MessageReady) {
    if (view.type == tandem::config::HidConfigMsgType::Command) {
      enqueue_command_locked(view.data, view.seq, false);
    }
  } else if (res == tandem::config::HidConfigReassembler::FeedResult::Overflow) {
    const uint8_t seq = (len >= 4U) ? data[3] : 0U;
    enqueue_command_locked(nullptr, seq, true);
  }
  portEXIT_CRITICAL(&rx_mux_);
}

bool HidConfigEndpoint::pop_command(char* out, size_t out_size, uint8_t& seq_out, bool& overflow_out) noexcept {
  if (out == nullptr || out_size == 0U) {
    return false;
  }
  bool has_item = false;
  portENTER_CRITICAL(&rx_mux_);
  if (cmd_count_ > 0U) {
    const QueuedCommand& slot = cmd_queue_[cmd_head_];
    seq_out = slot.seq;
    overflow_out = slot.overflow;
    if (!overflow_out) {
      std::strncpy(out, slot.line, out_size - 1U);
      out[out_size - 1U] = '\0';
    } else {
      out[0] = '\0';
    }
    cmd_head_ = (cmd_head_ + 1U) % kCommandQueueDepth;
    --cmd_count_;
    has_item = true;
  }
  portEXIT_CRITICAL(&rx_mux_);
  return has_item;
}

bool HidConfigEndpoint::enqueue_response(uint8_t seq, const char* data, size_t len) noexcept {
  return tx_queue_.enqueue_response(seq, data, len);
}

bool HidConfigEndpoint::enqueue_live(const char* data, size_t len) noexcept {
  return tx_queue_.enqueue_live(data, len);
}

bool HidConfigEndpoint::poll_tx(uint32_t now_ms, SendChunkFn send_fn, void* ctx, bool& stalled_out) noexcept {
  stalled_out = false;
  if (!tx_queue_.has_pending()) {
    tx_pending_since_valid_ = false;
    return false;
  }
  if (!tx_pending_since_valid_) {
    tx_pending_since_ms_ = now_ms;
    tx_pending_since_valid_ = true;
  }

  const uint8_t* chunk = nullptr;
  size_t len = 0U;
  if (send_fn != nullptr && tx_queue_.peek_chunk(chunk, len) && send_fn(ctx, chunk, len)) {
    tx_queue_.consume_chunk();
    tx_pending_since_ms_ = now_ms;
    if (!tx_queue_.has_pending()) {
      tx_pending_since_valid_ = false;
    }
    return true;
  }

  if (static_cast<uint32_t>(now_ms - tx_pending_since_ms_) >= kTxStallTimeoutMs) {
    tx_queue_.reset();
    tx_pending_since_valid_ = false;
    stalled_out = true;
  }
  return false;
}

void HidConfigEndpoint::reset_tx() noexcept {
  tx_queue_.reset();
  tx_pending_since_valid_ = false;
}

}  // namespace tandem::hal

#include "tandem/hid_config_transport.h"

#include <cstring>

#include "tandem/espnow_packet.h"

namespace tandem::config {
namespace {

bool is_valid_msg_type(uint8_t raw) noexcept {
  return raw == static_cast<uint8_t>(HidConfigMsgType::Command) || raw == static_cast<uint8_t>(HidConfigMsgType::Response) ||
         raw == static_cast<uint8_t>(HidConfigMsgType::Live);
}

bool is_valid_report_bytes(size_t report_bytes) noexcept {
  return report_bytes >= kHidConfigMinReportBytes && report_bytes <= kHidConfigMaxReportBytes;
}

void write_u16_le(uint8_t* dst, uint16_t val) noexcept {
  dst[0] = static_cast<uint8_t>(val & 0xFFU);
  dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
}

uint16_t read_u16_le(const uint8_t* src) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8U));
}

}  // namespace

bool HidConfigChunkEncoder::begin(HidConfigMsgType type, uint8_t seq, const uint8_t* payload, size_t payload_len, size_t report_bytes) noexcept {
  reset();
  if (!is_valid_msg_type(static_cast<uint8_t>(type)) || !is_valid_report_bytes(report_bytes)) {
    return false;
  }
  if (payload_len > 0U && payload == nullptr) {
    return false;
  }

  const size_t max_payload = report_bytes - kHidConfigChunkOverheadBytes;
  const size_t chunks = (payload_len == 0U) ? 1U : ((payload_len + max_payload - 1U) / max_payload);
  if (chunks > 256U) {
    return false;
  }

  type_ = type;
  seq_ = seq;
  payload_ = payload;
  payload_len_ = payload_len;
  offset_ = 0U;
  report_bytes_ = report_bytes;
  chunk_idx_ = 0U;
  total_chunks_ = static_cast<uint16_t>(chunks);
  active_ = true;
  return true;
}

bool HidConfigChunkEncoder::has_next() const noexcept {
  return active_ && chunk_idx_ < total_chunks_;
}

bool HidConfigChunkEncoder::next_chunk(uint8_t* out, size_t out_len) noexcept {
  if (!has_next() || out == nullptr || out_len < report_bytes_) {
    return false;
  }

  std::memset(out, 0, report_bytes_);

  const bool is_start = (chunk_idx_ == 0U);
  const bool is_end = (static_cast<uint16_t>(chunk_idx_ + 1U) == total_chunks_);
  const size_t remaining = payload_len_ - offset_;
  const size_t max_payload = report_bytes_ - kHidConfigChunkOverheadBytes;
  const size_t slice = (remaining < max_payload) ? remaining : max_payload;

  uint8_t flags = static_cast<uint8_t>(type_) & 0x0FU;
  if (is_start) {
    flags |= kHidConfigFlagStart;
  }
  if (is_end) {
    flags |= kHidConfigFlagEnd;
  }

  out[0] = kHidConfigFrameMagic;
  out[1] = kHidConfigFrameVersion;
  out[2] = flags;
  out[3] = seq_;
  out[4] = static_cast<uint8_t>(chunk_idx_);
  out[5] = static_cast<uint8_t>(slice);

  if (slice > 0U) {
    std::memcpy(&out[kHidConfigHeaderBytes], payload_ + offset_, slice);
  }

  const size_t crc_input_len = kHidConfigHeaderBytes + slice;
  const uint16_t crc = tandem::crc16_ccitt_false(out, crc_input_len);
  write_u16_le(&out[crc_input_len], crc);

  offset_ += slice;
  ++chunk_idx_;
  if (chunk_idx_ >= total_chunks_) {
    active_ = false;
  }
  return true;
}

void HidConfigChunkEncoder::reset() noexcept {
  type_ = HidConfigMsgType::Command;
  seq_ = 0U;
  payload_ = nullptr;
  payload_len_ = 0U;
  offset_ = 0U;
  chunk_idx_ = 0U;
  total_chunks_ = 0U;
  active_ = false;
}

HidConfigReassembler::HidConfigReassembler(char* storage, size_t capacity) noexcept : storage_(storage), capacity_(capacity) {
  reset();
}

void HidConfigReassembler::reset() noexcept {
  len_ = 0U;
  seq_ = 0U;
  next_idx_ = 0U;
  active_ = false;
  if (storage_ != nullptr && capacity_ > 0U) {
    storage_[0] = '\0';
  }
}

HidConfigReassembler::FeedResult HidConfigReassembler::push_chunk(const uint8_t* chunk, size_t chunk_len, HidConfigMessageView& out) noexcept {
  out = HidConfigMessageView{};

  if (storage_ == nullptr || capacity_ < 2U || chunk == nullptr || chunk_len < kHidConfigChunkOverheadBytes) {
    return FeedResult::InvalidChunk;
  }

  if (chunk[0] != kHidConfigFrameMagic) {
    return FeedResult::InvalidChunk;
  }
  if (chunk[1] != kHidConfigFrameVersion) {
    return FeedResult::UnsupportedVersion;
  }

  const uint8_t flags_type = chunk[2];
  if ((flags_type & kHidConfigReservedMask) != 0U) {
    return FeedResult::InvalidChunk;
  }

  const uint8_t raw_type = flags_type & 0x0FU;
  if (!is_valid_msg_type(raw_type)) {
    return FeedResult::InvalidChunk;
  }

  const bool is_start = (flags_type & kHidConfigFlagStart) != 0U;
  const bool is_end = (flags_type & kHidConfigFlagEnd) != 0U;
  const HidConfigMsgType msg_type = static_cast<HidConfigMsgType>(raw_type);
  const uint8_t seq = chunk[3];
  const uint8_t chunk_idx = chunk[4];
  const uint8_t payload_len = chunk[5];

  if (static_cast<size_t>(payload_len) + kHidConfigChunkOverheadBytes > chunk_len) {
    return FeedResult::InvalidChunk;
  }

  const size_t crc_input_len = kHidConfigHeaderBytes + static_cast<size_t>(payload_len);
  const uint16_t expected_crc = tandem::crc16_ccitt_false(chunk, crc_input_len);
  const uint16_t wire_crc = read_u16_le(&chunk[crc_input_len]);
  if (wire_crc != expected_crc) {
    return FeedResult::BadCrc;
  }

  if (is_start) {
    if (chunk_idx != 0U) {
      reset();
      return FeedResult::SequenceError;
    }
    reset();
    active_ = true;
    type_ = msg_type;
    seq_ = seq;
    next_idx_ = 0U;
  } else {
    if (!active_ || msg_type != type_ || seq != seq_ || chunk_idx != next_idx_) {
      reset();
      return FeedResult::SequenceError;
    }
  }

  if (len_ + static_cast<size_t>(payload_len) > capacity_ - 1U) {
    reset();
    return FeedResult::Overflow;
  }

  if (payload_len > 0U) {
    std::memcpy(storage_ + len_, &chunk[kHidConfigHeaderBytes], payload_len);
    len_ += static_cast<size_t>(payload_len);
  }

  if (is_end) {
    storage_[len_] = '\0';
    out.type = type_;
    out.seq = seq_;
    out.data = storage_;
    out.len = len_;
    active_ = false;
    return FeedResult::MessageReady;
  }

  const uint16_t next = static_cast<uint16_t>(next_idx_) + 1U;
  if (next >= 256U) {
    reset();
    return FeedResult::Overflow;
  }
  next_idx_ = static_cast<uint8_t>(next);
  return FeedResult::Incomplete;
}

HidConfigTxQueue::HidConfigTxQueue(size_t report_bytes) noexcept {
  if (!set_report_bytes(report_bytes)) {
    report_bytes_ = kHidConfigUsbReportBytes;
    reset();
  }
}

bool HidConfigTxQueue::set_report_bytes(size_t report_bytes) noexcept {
  if (!is_valid_report_bytes(report_bytes)) {
    return false;
  }
  report_bytes_ = report_bytes;
  reset();
  return true;
}

void HidConfigTxQueue::reset() noexcept {
  encoder_.reset();
  resp_len_ = 0U;
  resp_seq_ = 0U;
  has_resp_ = false;
  live_len_ = 0U;
  live_seq_ = 0U;
  has_live_ = false;
  has_current_chunk_ = false;
}

bool HidConfigTxQueue::enqueue_response(uint8_t seq, const char* data, size_t len) noexcept {
  if (len > kMaxResponseLength || (len > 0U && data == nullptr)) {
    return false;
  }
  const size_t max_payload = report_bytes_ - kHidConfigChunkOverheadBytes;
  if (len > max_payload * 256U) {
    return false;
  }

  if (!has_current_chunk_ && !encoder_.has_next() && !has_resp_) {
    if (len > 0U) {
      std::memcpy(active_buf_, data, len);
    }
    return encoder_.begin(HidConfigMsgType::Response, seq, reinterpret_cast<const uint8_t*>(active_buf_), len, report_bytes_);
  }

  if (len > 0U) {
    std::memcpy(resp_buf_, data, len);
  }
  resp_len_ = len;
  resp_seq_ = seq;
  has_resp_ = true;
  return true;
}

bool HidConfigTxQueue::enqueue_live(const char* data, size_t len) noexcept {
  if (len > kMaxResponseLength || (len > 0U && data == nullptr)) {
    return false;
  }
  const size_t max_payload = report_bytes_ - kHidConfigChunkOverheadBytes;
  if (len > max_payload * 256U) {
    return false;
  }

  if (len > 0U) {
    std::memcpy(live_buf_, data, len);
  }
  live_len_ = len;
  has_live_ = true;
  return true;
}

bool HidConfigTxQueue::start_next_message() noexcept {
  if (has_resp_) {
    if (resp_len_ > 0U) {
      std::memcpy(active_buf_, resp_buf_, resp_len_);
    }
    const uint8_t seq = resp_seq_;
    const size_t len = resp_len_;
    has_resp_ = false;
    resp_len_ = 0U;
    return encoder_.begin(HidConfigMsgType::Response, seq, reinterpret_cast<const uint8_t*>(active_buf_), len, report_bytes_);
  }

  if (has_live_) {
    if (live_len_ > 0U) {
      std::memcpy(active_buf_, live_buf_, live_len_);
    }
    const size_t len = live_len_;
    const uint8_t seq = live_seq_++;
    has_live_ = false;
    live_len_ = 0U;
    return encoder_.begin(HidConfigMsgType::Live, seq, reinterpret_cast<const uint8_t*>(active_buf_), len, report_bytes_);
  }

  return false;
}

bool HidConfigTxQueue::peek_chunk(const uint8_t*& data_out, size_t& len_out) noexcept {
  data_out = nullptr;
  len_out = 0U;

  if (!has_current_chunk_) {
    if (!encoder_.has_next() && !start_next_message()) {
      return false;
    }
    if (!encoder_.next_chunk(current_chunk_, report_bytes_)) {
      return false;
    }
    has_current_chunk_ = true;
  }

  data_out = current_chunk_;
  len_out = report_bytes_;
  return true;
}

void HidConfigTxQueue::consume_chunk() noexcept {
  has_current_chunk_ = false;
}

bool HidConfigTxQueue::has_pending() const noexcept {
  return has_current_chunk_ || encoder_.has_next() || has_resp_ || has_live_;
}

}  // namespace tandem::config

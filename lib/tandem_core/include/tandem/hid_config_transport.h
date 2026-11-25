#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/config_protocol.h"

namespace tandem::config {

// Transport-agnostic HID Vendor-Defined framing for WebHID over USB and BLE HOGP.
//
// Multi-chunk report splitting mirrors jfedor2/hid-remapper (MIT) fixed-report chunking,
// while the SEQ + CRC-16 request/response framing pattern is adapted from Joypad OS
// cdc_protocol.h (Apache-2.0). Never send raw unversioned C structs over the wire.

inline constexpr uint16_t kHidConfigUsagePage = 0xFF54U;  // 'T' vendor page
inline constexpr uint16_t kHidConfigUsageApp = 0x004DU;   // 'M' application usage
inline constexpr uint8_t kHidConfigUsageInput = 0x01U;
inline constexpr uint8_t kHidConfigUsageOutput = 0x02U;

inline constexpr uint8_t kHidReportIdConfigUsb = 3U;
inline constexpr size_t kHidConfigUsbReportBytes = 63U;
inline constexpr size_t kHidConfigBleReportBytes = 20U;

inline constexpr uint8_t kHidConfigFrameMagic = 0x54U;  // 'T'
inline constexpr uint8_t kHidConfigFrameVersion = 1U;
inline constexpr size_t kHidConfigHeaderBytes = 6U;
inline constexpr size_t kHidConfigCrcBytes = 2U;
inline constexpr size_t kHidConfigChunkOverheadBytes = kHidConfigHeaderBytes + kHidConfigCrcBytes;
inline constexpr size_t kHidConfigMinReportBytes = kHidConfigChunkOverheadBytes + 1U;
inline constexpr size_t kHidConfigMaxReportBytes = 255U;

inline constexpr uint8_t kHidConfigFlagStart = 0x10U;
inline constexpr uint8_t kHidConfigFlagEnd = 0x20U;
inline constexpr uint8_t kHidConfigReservedMask = 0xC0U;

inline constexpr size_t kHidConfigReportDescriptorBytes = 36U;

enum class HidConfigMsgType : uint8_t {
  Command = 1U,
  Response = 2U,
  Live = 3U,
};

struct HidConfigReportDescriptor {
  uint8_t bytes[kHidConfigReportDescriptorBytes]{};
};

static_assert(sizeof(HidConfigReportDescriptor) == kHidConfigReportDescriptorBytes,
              "HidConfigReportDescriptor must match kHidConfigReportDescriptorBytes");

/// Builds a HID 1.11 Vendor-Defined Top-Level Collection descriptor with the given
/// Report ID and fixed byte-array report payload size (`report_bytes`, excluding Report ID).
constexpr HidConfigReportDescriptor make_hid_config_report_descriptor(uint8_t report_id, uint8_t report_bytes) noexcept {
  HidConfigReportDescriptor desc{{
      0x06U,
      static_cast<uint8_t>(kHidConfigUsagePage & 0xFFU),
      static_cast<uint8_t>((kHidConfigUsagePage >> 8U) & 0xFFU),
      0x09U,
      static_cast<uint8_t>(kHidConfigUsageApp & 0xFFU),
      0xA1U,
      0x01U,  // Collection (Application)
      0x85U,
      report_id,  // Report ID
      0x09U,
      kHidConfigUsageInput,  // Usage (Vendor Input)
      0x15U,
      0x00U,  // Logical Minimum (0)
      0x26U,
      0xFFU,
      0x00U,  // Logical Maximum (255, unsigned 2-byte form)
      0x75U,
      0x08U,  // Report Size (8 bits)
      0x95U,
      report_bytes,  // Report Count (report_bytes)
      0x81U,
      0x02U,  // Input (Data, Var, Abs)
      0x09U,
      kHidConfigUsageOutput,  // Usage (Vendor Output)
      0x15U,
      0x00U,  // Logical Minimum (0)
      0x26U,
      0xFFU,
      0x00U,  // Logical Maximum (255, unsigned 2-byte form)
      0x75U,
      0x08U,  // Report Size (8 bits)
      0x95U,
      report_bytes,  // Report Count (report_bytes)
      0x91U,
      0x02U,  // Output (Data, Var, Abs)
      0xC0U,  // End Collection
  }};
  return desc;
}

inline constexpr HidConfigReportDescriptor kHidConfigUsbReportDescriptor =
    make_hid_config_report_descriptor(kHidReportIdConfigUsb, static_cast<uint8_t>(kHidConfigUsbReportBytes));

/// Splits an arbitrary config command/response/live frame into fixed-size zero-padded
/// HID chunks with sequence numbering and per-chunk CRC-16/CCITT-FALSE integrity checks.
class HidConfigChunkEncoder {
 public:
  /// Starts encoding `payload[0..payload_len)` for reports of `report_bytes` bytes.
  /// Returns false on invalid arguments or if chunk count would exceed 256.
  bool begin(HidConfigMsgType type, uint8_t seq, const uint8_t* payload, size_t payload_len, size_t report_bytes = kHidConfigUsbReportBytes) noexcept;

  /// Returns true when at least one chunk remains to be emitted.
  bool has_next() const noexcept;

  /// Writes the next zero-padded `report_bytes`-byte chunk into `out`.
  bool next_chunk(uint8_t* out, size_t out_len) noexcept;

  /// Resets the encoder to the idle state.
  void reset() noexcept;

 private:
  HidConfigMsgType type_{HidConfigMsgType::Command};
  uint8_t seq_{0};
  const uint8_t* payload_{nullptr};
  size_t payload_len_{0};
  size_t offset_{0};
  size_t report_bytes_{kHidConfigUsbReportBytes};
  uint16_t chunk_idx_{0};
  uint16_t total_chunks_{0};
  bool active_{false};
};

struct HidConfigMessageView {
  HidConfigMsgType type{HidConfigMsgType::Command};
  uint8_t seq{0};
  const char* data{nullptr};
  size_t len{0};
};

/// Reassembles fixed-size HID chunks into a null-terminated message without heap allocation.
class HidConfigReassembler {
 public:
  enum class FeedResult : uint8_t {
    Incomplete = 0,
    MessageReady = 1,
    InvalidChunk = 2,
    UnsupportedVersion = 3,
    BadCrc = 4,
    SequenceError = 5,
    Overflow = 6,
  };

  HidConfigReassembler(char* storage, size_t capacity) noexcept;

  /// Clears any partial message assembly state.
  void reset() noexcept;

  /// Validates and consumes one HID report payload (excluding HID Report ID).
  FeedResult push_chunk(const uint8_t* chunk, size_t chunk_len, HidConfigMessageView& out) noexcept;

 private:
  char* storage_{nullptr};
  size_t capacity_{0};
  size_t len_{0};
  HidConfigMsgType type_{HidConfigMsgType::Command};
  uint8_t seq_{0};
  uint8_t next_idx_{0};
  bool active_{false};
};

/// Convenience stack/static storage wrapper around HidConfigReassembler.
template <size_t Capacity>
class FixedHidConfigReassembler final : public HidConfigReassembler {
 public:
  static_assert(Capacity >= 2U, "FixedHidConfigReassembler capacity must be >= 2");

  FixedHidConfigReassembler() noexcept : HidConfigReassembler(buf_, Capacity) {}

 private:
  char buf_[Capacity]{};
};

/// Non-blocking zero-heap TX queue prioritizing command responses over live telemetry frames.
class HidConfigTxQueue {
 public:
  explicit HidConfigTxQueue(size_t report_bytes = kHidConfigUsbReportBytes) noexcept;

  /// Sets the HID Input report payload size (in bytes, excluding Report ID) and resets state.
  bool set_report_bytes(size_t report_bytes) noexcept;

  /// Clears active and pending messages.
  void reset() noexcept;

  /// Enqueues a response frame (`@TANDEM ...\n`), replacing any queued unsent response.
  bool enqueue_response(uint8_t seq, const char* data, size_t len) noexcept;

  /// Enqueues a live telemetry frame (`@TANDEM ...\n`), coalescing to the latest sample.
  bool enqueue_live(const char* data, size_t len) noexcept;

  /// Returns a stable pointer to the current chunk until `consume_chunk()` or `reset()`.
  bool peek_chunk(const uint8_t*& data_out, size_t& len_out) noexcept;

  /// Marks the chunk returned by `peek_chunk()` as transmitted.
  void consume_chunk() noexcept;

  /// Returns true if any chunk or queued message is waiting to be transmitted.
  bool has_pending() const noexcept;

 private:
  bool start_next_message() noexcept;

  size_t report_bytes_{kHidConfigUsbReportBytes};
  HidConfigChunkEncoder encoder_{};
  char active_buf_[kMaxResponseLength]{};
  char resp_buf_[kMaxResponseLength]{};
  size_t resp_len_{0};
  uint8_t resp_seq_{0};
  bool has_resp_{false};

  char live_buf_[kMaxResponseLength]{};
  size_t live_len_{0};
  uint8_t live_seq_{0};
  bool has_live_{false};

  uint8_t current_chunk_[kHidConfigMaxReportBytes]{};
  bool has_current_chunk_{false};
};

}  // namespace tandem::config

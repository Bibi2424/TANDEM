// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/types.h"

namespace tandem {

// Versioned 30-byte little-endian ESP-NOW wire packet contract:
//   [0..1]   magic ("TM" = 0x54, 0x4D)
//   [2]      version (kEspNowPacketVersion = 1)
//   [3]      stick_id (StickId::Left = 1 or StickId::Right = 2)
//   [4..5]   seq (uint16_t LE)
//   [6..21]  axes[0..7] (8 x int16_t LE, clamped to [kAxisMin, kAxisMax])
//   [22..25] buttons (uint32_t LE)
//   [26]     hat (uint8_t, 0..8)
//   [27]     flags (uint8_t)
//   [28..29] crc16 (uint16_t LE, CRC-16/CCITT-FALSE over bytes [0..27])
inline constexpr uint8_t kEspNowPacketMagic0 = 0x54U;  // 'T'
inline constexpr uint8_t kEspNowPacketMagic1 = 0x4DU;  // 'M'
inline constexpr uint8_t kEspNowPacketVersion = 1U;
inline constexpr size_t kEspNowPacketBytes = 30U;

struct EspNowPacketBuffer {
  uint8_t bytes[kEspNowPacketBytes]{};
};

static_assert(sizeof(EspNowPacketBuffer) == kEspNowPacketBytes, "EspNowPacketBuffer must be exactly 30 bytes");

enum class PacketDecodeResult : uint8_t {
  Ok = 0,
  InvalidLength = 1,
  BadMagic = 2,
  UnsupportedVersion = 3,
  InvalidStickId = 4,
  BadCrc = 5,
};

struct DecodedEspNowPacket {
  StickId stick_id{StickId::Unknown};
  uint16_t seq{0};
  StickState state{};
};

/// Computes CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over `data[0..len-1]`.
uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) noexcept;

/// Encodes `stick_id`, `seq`, and `state` into a 30-byte little-endian ESP-NOW packet.
/// Returns false if `out` is null, `out_len < kEspNowPacketBytes`, or `stick_id` is not Left/Right.
bool encode_espnow_packet(StickId stick_id, uint16_t seq, const StickState& state, uint8_t* out, size_t out_len) noexcept;

/// Validates and decodes a 30-byte ESP-NOW packet into `out`, sanitizing axes and hat.
PacketDecodeResult decode_espnow_packet(const uint8_t* data, size_t len, DecodedEspNowPacket& out) noexcept;

/// Wraparound-safe uint16_t sequence delta (`next_seq - prev_seq`).
constexpr uint16_t espnow_seq_delta(uint16_t prev_seq, uint16_t next_seq) noexcept {
  return static_cast<uint16_t>(next_seq - prev_seq);
}

/// Lightweight RX sequence tracker for drop-rate and out-of-order diagnostics.
struct EspNowLinkStats {
  uint32_t rx_packets{0};
  uint32_t dropped_packets{0};
  uint32_t out_of_order_or_dup{0};
  uint16_t last_seq{0};
  bool has_rx{false};

  void record_seq(uint16_t seq) noexcept;
  void reset() noexcept;
};

}  // namespace tandem

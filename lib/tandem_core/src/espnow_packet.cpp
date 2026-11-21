#include "tandem/espnow_packet.h"

namespace tandem {
namespace {

constexpr uint8_t kMaxValidHat = 8U;
constexpr size_t kCrcPayloadBytes = kEspNowPacketBytes - 2U;

bool is_valid_stick_id(StickId id) noexcept {
  return id == StickId::Left || id == StickId::Right;
}

int16_t clamp_axis_sym(int16_t value) noexcept {
  if (value < kAxisMin) {
    return kAxisMin;
  }
  if (value > kAxisMax) {
    return kAxisMax;
  }
  return value;
}

uint8_t sanitize_hat(uint8_t hat) noexcept {
  return (hat <= kMaxValidHat) ? hat : kHatCentered;
}

void write_u16_le(uint8_t* dst, uint16_t val) noexcept {
  dst[0] = static_cast<uint8_t>(val & 0xFFU);
  dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
}

void write_u32_le(uint8_t* dst, uint32_t val) noexcept {
  dst[0] = static_cast<uint8_t>(val & 0xFFU);
  dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
  dst[2] = static_cast<uint8_t>((val >> 16U) & 0xFFU);
  dst[3] = static_cast<uint8_t>((val >> 24U) & 0xFFU);
}

uint16_t read_u16_le(const uint8_t* src) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) |
                               (static_cast<uint16_t>(src[1]) << 8U));
}

uint32_t read_u32_le(const uint8_t* src) noexcept {
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8U) |
         (static_cast<uint32_t>(src[2]) << 16U) |
         (static_cast<uint32_t>(src[3]) << 24U);
}

}  // namespace

uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) noexcept {
  uint16_t crc = 0xFFFFU;
  if (data == nullptr) {
    return crc;
  }
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1U);
      }
    }
  }
  return crc;
}

bool encode_espnow_packet(StickId stick_id,
                          uint16_t seq,
                          const StickState& state,
                          uint8_t* out,
                          size_t out_len) noexcept {
  if (out == nullptr || out_len < kEspNowPacketBytes || !is_valid_stick_id(stick_id)) {
    return false;
  }

  out[0] = kEspNowPacketMagic0;
  out[1] = kEspNowPacketMagic1;
  out[2] = kEspNowPacketVersion;
  out[3] = static_cast<uint8_t>(stick_id);
  write_u16_le(&out[4], seq);

  size_t offset = 6U;
  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    const uint16_t raw = static_cast<uint16_t>(clamp_axis_sym(state.axes[i]));
    write_u16_le(&out[offset], raw);
    offset += 2U;
  }

  write_u32_le(&out[22], state.buttons);
  out[26] = sanitize_hat(state.hat);
  out[27] = state.flags;

  const uint16_t crc = crc16_ccitt_false(out, kCrcPayloadBytes);
  write_u16_le(&out[28], crc);
  return true;
}

PacketDecodeResult decode_espnow_packet(const uint8_t* data,
                                        size_t len,
                                        DecodedEspNowPacket& out) noexcept {
  if (data == nullptr || len != kEspNowPacketBytes) {
    return PacketDecodeResult::InvalidLength;
  }
  if (data[0] != kEspNowPacketMagic0 || data[1] != kEspNowPacketMagic1) {
    return PacketDecodeResult::BadMagic;
  }
  if (data[2] != kEspNowPacketVersion) {
    return PacketDecodeResult::UnsupportedVersion;
  }

  const StickId id = static_cast<StickId>(data[3]);
  if (!is_valid_stick_id(id)) {
    return PacketDecodeResult::InvalidStickId;
  }

  const uint16_t expected_crc = crc16_ccitt_false(data, kCrcPayloadBytes);
  const uint16_t wire_crc = read_u16_le(&data[28]);
  if (wire_crc != expected_crc) {
    return PacketDecodeResult::BadCrc;
  }

  out.stick_id = id;
  out.seq = read_u16_le(&data[4]);
  out.state = make_centered_state(data[27]);

  size_t offset = 6U;
  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    const int16_t raw = static_cast<int16_t>(read_u16_le(&data[offset]));
    out.state.axes[i] = clamp_axis_sym(raw);
    offset += 2U;
  }

  out.state.buttons = read_u32_le(&data[22]);
  out.state.hat = sanitize_hat(data[26]);
  out.state.reserved[0] = 0;
  out.state.reserved[1] = 0;
  return PacketDecodeResult::Ok;
}

void EspNowLinkStats::record_seq(uint16_t seq) noexcept {
  if (!has_rx) {
    has_rx = true;
    last_seq = seq;
    ++rx_packets;
    return;
  }

  const uint16_t delta = espnow_seq_delta(last_seq, seq);
  if (delta == 0U || delta >= 0x8000U) {
    ++out_of_order_or_dup;
    return;
  }

  ++rx_packets;
  dropped_packets += static_cast<uint32_t>(delta - 1U);
  last_seq = seq;
}

void EspNowLinkStats::reset() noexcept {
  rx_packets = 0;
  dropped_packets = 0;
  out_of_order_or_dup = 0;
  last_seq = 0;
  has_rx = false;
}

}  // namespace tandem

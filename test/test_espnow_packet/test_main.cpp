#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <unity.h>

#include "tandem/espnow_packet.h"

static_assert(__cplusplus >= 201703L, "Native tests expect C++17 or newer");

void setUp() {}
void tearDown() {}

void test_crc16_ccitt_false_standard_vector() {
  const uint8_t vec[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1U, tandem::crc16_ccitt_false(vec, sizeof(vec)));
  TEST_ASSERT_EQUAL_HEX16(0xFFFFU, tandem::crc16_ccitt_false(nullptr, 5U));
  TEST_ASSERT_EQUAL_HEX16(0xFFFFU, tandem::crc16_ccitt_false(vec, 0U));
}

void test_round_trip_left_and_right_sticks() {
  tandem::StickState tx = tandem::make_centered_state(tandem::kStateFlagConnected);
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    tx.axes[i] = static_cast<int16_t>((static_cast<int32_t>(i) - 3) * 4000);
  }
  tx.buttons = 0xA55A1234U;
  tx.hat = 6U;
  tx.reserved[0] = 0xFFU;
  tx.reserved[1] = 0xEEU;

  for (tandem::StickId id : {tandem::StickId::Left, tandem::StickId::Right}) {
    tandem::EspNowPacketBuffer pkt{};
    TEST_ASSERT_TRUE(tandem::encode_espnow_packet(
        id, 0xBEEFU, tx, pkt.bytes, sizeof(pkt.bytes)));

    tandem::DecodedEspNowPacket rx{};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(tandem::PacketDecodeResult::Ok),
        static_cast<uint8_t>(
            tandem::decode_espnow_packet(pkt.bytes, sizeof(pkt.bytes), rx)));

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(id),
                            static_cast<uint8_t>(rx.stick_id));
    TEST_ASSERT_EQUAL_UINT16(0xBEEFU, rx.seq);
    for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
      TEST_ASSERT_EQUAL_INT16(tx.axes[i], rx.state.axes[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(tx.buttons, rx.state.buttons);
    TEST_ASSERT_EQUAL_UINT8(tx.hat, rx.state.hat);
    TEST_ASSERT_EQUAL_UINT8(tx.flags, rx.state.flags);
    TEST_ASSERT_EQUAL_UINT8(0U, rx.state.reserved[0]);
    TEST_ASSERT_EQUAL_UINT8(0U, rx.state.reserved[1]);
  }
}

void test_exact_wire_layout_and_little_endian() {
  tandem::StickState state = tandem::make_centered_state(tandem::kStateFlagConnected);
  state.axes[0] = tandem::kAxisMin;  // -32767 = 0x8001 -> LE: 0x01, 0x80
  state.axes[1] = tandem::kAxisMax;  // +32767 = 0x7FFF -> LE: 0xFF, 0x7F
  state.buttons = 0x12345678U;       // LE: 0x78, 0x56, 0x34, 0x12
  state.hat = 4U;

  tandem::EspNowPacketBuffer pkt{};
  TEST_ASSERT_TRUE(tandem::encode_espnow_packet(
      tandem::StickId::Left, 0x1234U, state, pkt.bytes, sizeof(pkt.bytes)));

  TEST_ASSERT_EQUAL_HEX8(0x54U, pkt.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x4DU, pkt.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(1U, pkt.bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickId::Left), pkt.bytes[3]);
  TEST_ASSERT_EQUAL_HEX8(0x34U, pkt.bytes[4]);
  TEST_ASSERT_EQUAL_HEX8(0x12U, pkt.bytes[5]);
  TEST_ASSERT_EQUAL_HEX8(0x01U, pkt.bytes[6]);
  TEST_ASSERT_EQUAL_HEX8(0x80U, pkt.bytes[7]);
  TEST_ASSERT_EQUAL_HEX8(0xFFU, pkt.bytes[8]);
  TEST_ASSERT_EQUAL_HEX8(0x7FU, pkt.bytes[9]);
  TEST_ASSERT_EQUAL_HEX8(0x78U, pkt.bytes[22]);
  TEST_ASSERT_EQUAL_HEX8(0x56U, pkt.bytes[23]);
  TEST_ASSERT_EQUAL_HEX8(0x34U, pkt.bytes[24]);
  TEST_ASSERT_EQUAL_HEX8(0x12U, pkt.bytes[25]);
  TEST_ASSERT_EQUAL_UINT8(4U, pkt.bytes[26]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, pkt.bytes[27]);

  const uint16_t expected_crc = tandem::crc16_ccitt_false(pkt.bytes, 28U);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected_crc & 0xFFU), pkt.bytes[28]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((expected_crc >> 8U) & 0xFFU),
                         pkt.bytes[29]);
}

void test_encode_guards_null_short_and_invalid_stick_id() {
  const tandem::StickState state = tandem::make_centered_state();
  tandem::EspNowPacketBuffer pkt{};

  TEST_ASSERT_FALSE(tandem::encode_espnow_packet(
      tandem::StickId::Left, 0U, state, nullptr, tandem::kEspNowPacketBytes));
  TEST_ASSERT_FALSE(tandem::encode_espnow_packet(
      tandem::StickId::Left, 0U, state, pkt.bytes, tandem::kEspNowPacketBytes - 1U));
  TEST_ASSERT_FALSE(tandem::encode_espnow_packet(
      tandem::StickId::Unknown, 0U, state, pkt.bytes, sizeof(pkt.bytes)));
  TEST_ASSERT_FALSE(tandem::encode_espnow_packet(
      static_cast<tandem::StickId>(3U), 0U, state, pkt.bytes, sizeof(pkt.bytes)));
}

void test_decode_invalid_length_magic_version_and_stick_id() {
  const tandem::StickState state = tandem::make_centered_state();
  tandem::EspNowPacketBuffer valid{};
  TEST_ASSERT_TRUE(tandem::encode_espnow_packet(
      tandem::StickId::Right, 1U, state, valid.bytes, sizeof(valid.bytes)));

  tandem::DecodedEspNowPacket rx{};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::InvalidLength),
      static_cast<uint8_t>(tandem::decode_espnow_packet(nullptr, 30U, rx)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::InvalidLength),
      static_cast<uint8_t>(tandem::decode_espnow_packet(valid.bytes, 29U, rx)));

  uint8_t oversized[31]{};
  std::memcpy(oversized, valid.bytes, sizeof(valid.bytes));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::InvalidLength),
      static_cast<uint8_t>(tandem::decode_espnow_packet(oversized, sizeof(oversized), rx)));

  tandem::EspNowPacketBuffer bad_magic = valid;
  bad_magic.bytes[0] = 0x00U;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::BadMagic),
      static_cast<uint8_t>(
          tandem::decode_espnow_packet(bad_magic.bytes, sizeof(bad_magic.bytes), rx)));

  tandem::EspNowPacketBuffer bad_ver = valid;
  bad_ver.bytes[2] = 2U;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::UnsupportedVersion),
      static_cast<uint8_t>(
          tandem::decode_espnow_packet(bad_ver.bytes, sizeof(bad_ver.bytes), rx)));

  tandem::EspNowPacketBuffer bad_id = valid;
  bad_id.bytes[3] = 0U;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::InvalidStickId),
      static_cast<uint8_t>(
          tandem::decode_espnow_packet(bad_id.bytes, sizeof(bad_id.bytes), rx)));
}

void test_decode_single_bit_crc_corruption_every_byte() {
  const tandem::StickState state = tandem::make_centered_state(tandem::kStateFlagConnected);
  tandem::EspNowPacketBuffer valid{};
  TEST_ASSERT_TRUE(tandem::encode_espnow_packet(
      tandem::StickId::Left, 42U, state, valid.bytes, sizeof(valid.bytes)));

  for (size_t i = 0; i < tandem::kEspNowPacketBytes; ++i) {
    tandem::EspNowPacketBuffer corrupted = valid;
    corrupted.bytes[i] ^= 0x01U;
    tandem::DecodedEspNowPacket rx{};
    const tandem::PacketDecodeResult res =
        tandem::decode_espnow_packet(corrupted.bytes, sizeof(corrupted.bytes), rx);
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(tandem::PacketDecodeResult::Ok),
                          static_cast<uint8_t>(res));
    if (i >= 4U) {
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(tandem::PacketDecodeResult::BadCrc),
          static_cast<uint8_t>(res));
    }
  }
}

void test_encode_and_decode_sanitize_int16_min_and_invalid_hat() {
  tandem::StickState dirty = tandem::make_centered_state();
  dirty.axes[0] = INT16_MIN;
  dirty.hat = 9U;

  tandem::EspNowPacketBuffer pkt{};
  TEST_ASSERT_TRUE(tandem::encode_espnow_packet(
      tandem::StickId::Left, 7U, dirty, pkt.bytes, sizeof(pkt.bytes)));

  tandem::DecodedEspNowPacket rx{};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::Ok),
      static_cast<uint8_t>(
          tandem::decode_espnow_packet(pkt.bytes, sizeof(pkt.bytes), rx)));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, rx.state.axes[0]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kHatCentered, rx.state.hat);

  // Also test raw wire INT16_MIN + hat=15 with valid CRC on decode path.
  pkt.bytes[6] = 0x00U;
  pkt.bytes[7] = 0x80U;
  pkt.bytes[26] = 15U;
  const uint16_t crc = tandem::crc16_ccitt_false(pkt.bytes, 28U);
  pkt.bytes[28] = static_cast<uint8_t>(crc & 0xFFU);
  pkt.bytes[29] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(tandem::PacketDecodeResult::Ok),
      static_cast<uint8_t>(
          tandem::decode_espnow_packet(pkt.bytes, sizeof(pkt.bytes), rx)));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, rx.state.axes[0]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kHatCentered, rx.state.hat);
}

void test_seq_delta_and_drop_counter_wraparound() {
  TEST_ASSERT_EQUAL_UINT16(3U, tandem::espnow_seq_delta(0xFFFEU, 0x0001U));

  tandem::EspNowLinkStats stats{};
  stats.record_seq(0xFFFEU);
  stats.record_seq(0xFFFFU);
  stats.record_seq(0x0000U);  // wraparound, 0 dropped
  stats.record_seq(0x0003U);  // 0x0001 and 0x0002 dropped -> +2
  stats.record_seq(0x0003U);  // duplicate -> out_of_order_or_dup
  stats.record_seq(0x0002U);  // stale -> out_of_order_or_dup

  TEST_ASSERT_EQUAL_UINT32(4U, stats.rx_packets);
  TEST_ASSERT_EQUAL_UINT32(2U, stats.dropped_packets);
  TEST_ASSERT_EQUAL_UINT32(2U, stats.out_of_order_or_dup);
  TEST_ASSERT_EQUAL_UINT16(0x0003U, stats.last_seq);

  stats.reset();
  TEST_ASSERT_EQUAL_UINT32(0U, stats.rx_packets);
  TEST_ASSERT_EQUAL_UINT32(0U, stats.dropped_packets);
  TEST_ASSERT_EQUAL_UINT32(0U, stats.out_of_order_or_dup);
  TEST_ASSERT_FALSE(stats.has_rx);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_ccitt_false_standard_vector);
  RUN_TEST(test_round_trip_left_and_right_sticks);
  RUN_TEST(test_exact_wire_layout_and_little_endian);
  RUN_TEST(test_encode_guards_null_short_and_invalid_stick_id);
  RUN_TEST(test_decode_invalid_length_magic_version_and_stick_id);
  RUN_TEST(test_decode_single_bit_crc_corruption_every_byte);
  RUN_TEST(test_encode_and_decode_sanitize_int16_min_and_invalid_hat);
  RUN_TEST(test_seq_delta_and_drop_counter_wraparound);
  return UNITY_END();
}

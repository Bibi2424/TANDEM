#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "tandem/espnow_packet.h"
#include "tandem/hid_config_transport.h"

using namespace tandem::config;

void setUp() {}
void tearDown() {}

namespace {

void verify_descriptor(const HidConfigReportDescriptor& desc,
                       uint8_t expected_id,
                       uint8_t expected_bytes) {
  TEST_ASSERT_EQUAL_UINT32(36U, sizeof(desc.bytes));
  // Usage Page (0xFF54 LE)
  TEST_ASSERT_EQUAL_HEX8(0x06U, desc.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x54U, desc.bytes[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFFU, desc.bytes[2]);
  // Usage (0x4D), Collection (Application), Report ID
  TEST_ASSERT_EQUAL_HEX8(0x09U, desc.bytes[3]);
  TEST_ASSERT_EQUAL_HEX8(0x4DU, desc.bytes[4]);
  TEST_ASSERT_EQUAL_HEX8(0xA1U, desc.bytes[5]);
  TEST_ASSERT_EQUAL_HEX8(0x01U, desc.bytes[6]);
  TEST_ASSERT_EQUAL_HEX8(0x85U, desc.bytes[7]);
  TEST_ASSERT_EQUAL_UINT8(expected_id, desc.bytes[8]);

  // Input report: Logical Max 255 in 2-byte unsigned form (0x26 0xFF 0x00)
  TEST_ASSERT_EQUAL_HEX8(0x26U, desc.bytes[13]);
  TEST_ASSERT_EQUAL_HEX8(0xFFU, desc.bytes[14]);
  TEST_ASSERT_EQUAL_HEX8(0x00U, desc.bytes[15]);
  TEST_ASSERT_EQUAL_HEX8(0x95U, desc.bytes[18]);
  TEST_ASSERT_EQUAL_UINT8(expected_bytes, desc.bytes[19]);
  TEST_ASSERT_EQUAL_HEX8(0x81U, desc.bytes[20]);

  // Output report: Logical Max 255 in 2-byte unsigned form + Report Count
  TEST_ASSERT_EQUAL_HEX8(0x26U, desc.bytes[26]);
  TEST_ASSERT_EQUAL_HEX8(0xFFU, desc.bytes[27]);
  TEST_ASSERT_EQUAL_HEX8(0x00U, desc.bytes[28]);
  TEST_ASSERT_EQUAL_HEX8(0x95U, desc.bytes[31]);
  TEST_ASSERT_EQUAL_UINT8(expected_bytes, desc.bytes[32]);
  TEST_ASSERT_EQUAL_HEX8(0x91U, desc.bytes[33]);

  // End Collection
  TEST_ASSERT_EQUAL_HEX8(0xC0U, desc.bytes[35]);
}

void round_trip_message(HidConfigMsgType type,
                        uint8_t seq,
                        const std::string& payload,
                        size_t report_bytes) {
  HidConfigChunkEncoder encoder;
  TEST_ASSERT_TRUE(encoder.begin(
      type,
      seq,
      reinterpret_cast<const uint8_t*>(payload.data()),
      payload.size(),
      report_bytes));

  FixedHidConfigReassembler<kMaxResponseLength> reassembler;
  HidConfigMessageView view{};
  uint8_t chunk[kHidConfigMaxReportBytes]{};
  size_t chunks = 0;

  while (encoder.has_next()) {
    std::memset(chunk, 0xAAU, sizeof(chunk));
    TEST_ASSERT_TRUE(encoder.next_chunk(chunk, report_bytes));
    ++chunks;

    const uint8_t payload_len = chunk[5];
    for (size_t pad = kHidConfigChunkOverheadBytes + payload_len;
         pad < report_bytes;
         ++pad) {
      TEST_ASSERT_EQUAL_HEX8(0x00U, chunk[pad]);
    }

    const auto res = reassembler.push_chunk(chunk, report_bytes, view);
    if (encoder.has_next()) {
      TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::Incomplete, res);
    } else {
      TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::MessageReady, res);
    }
  }

  TEST_ASSERT_GREATER_THAN_UINT32(0U, static_cast<uint32_t>(chunks));
  TEST_ASSERT_EQUAL(type, view.type);
  TEST_ASSERT_EQUAL_UINT8(seq, view.seq);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(payload.size()),
                           static_cast<uint32_t>(view.len));
  TEST_ASSERT_EQUAL_STRING(payload.c_str(), view.data);
}

}  // namespace

void test_descriptor_builder_usb_and_ble() {
  verify_descriptor(kHidConfigUsbReportDescriptor,
                    kHidReportIdConfigUsb,
                    static_cast<uint8_t>(kHidConfigUsbReportBytes));
  const auto ble_desc = make_hid_config_report_descriptor(
      2U, static_cast<uint8_t>(kHidConfigBleReportBytes));
  verify_descriptor(
      ble_desc, 2U, static_cast<uint8_t>(kHidConfigBleReportBytes));
}

void test_round_trip_single_zero_and_multichunk_usb_and_ble() {
  round_trip_message(HidConfigMsgType::Command, 7U, "$TANDEM caps\n", kHidConfigUsbReportBytes);
  round_trip_message(HidConfigMsgType::Response, 42U, "", kHidConfigUsbReportBytes);
  round_trip_message(HidConfigMsgType::Command, 99U, "$TANDEM set 0 1 10 2048 4090 0.05 0 1 0.35\n", kHidConfigBleReportBytes);

  std::string large = "@TANDEM {\"v\":1,\"cmd\":\"get\",\"ok\":true,\"axes\":[";
  for (int i = 0; i < 8; ++i) {
    large += (i == 0) ? "" : ",";
    large += "{\"i\":0,\"en\":1,\"min\":0,\"ctr\":2048,\"max\":4095,\"dz\":0.0500,\"inv\":0,\"crv\":1,\"p\":0.3500}";
  }
  large += "]}\n";
  TEST_ASSERT_GREATER_THAN(700U, large.size());

  round_trip_message(HidConfigMsgType::Response, 0xFEU, large, kHidConfigUsbReportBytes);
  round_trip_message(HidConfigMsgType::Live, 0x01U, large, kHidConfigBleReportBytes);
}

void test_encoder_validation_guards() {
  HidConfigChunkEncoder enc;
  const uint8_t sample[4]{'a', 'b', 'c', 'd'};

  TEST_ASSERT_FALSE(enc.begin(HidConfigMsgType::Command, 1U, nullptr, 4U, kHidConfigUsbReportBytes));
  TEST_ASSERT_FALSE(enc.begin(HidConfigMsgType::Command, 1U, sample, 4U, kHidConfigMinReportBytes - 1U));
  TEST_ASSERT_FALSE(enc.begin(HidConfigMsgType::Command, 1U, sample, 4U, kHidConfigMaxReportBytes + 1U));
  TEST_ASSERT_FALSE(enc.begin(static_cast<HidConfigMsgType>(99U), 1U, sample, 4U, kHidConfigUsbReportBytes));

  // Exceeding 256 chunks with minimal 9-byte report (1 payload byte/chunk) must fail.
  std::vector<uint8_t> too_many(257U, 'X');
  TEST_ASSERT_FALSE(enc.begin(HidConfigMsgType::Response, 1U, too_many.data(), too_many.size(), kHidConfigMinReportBytes));

  uint8_t small_out[8]{};
  TEST_ASSERT_TRUE(enc.begin(HidConfigMsgType::Command, 1U, sample, 4U, kHidConfigUsbReportBytes));
  TEST_ASSERT_FALSE(enc.next_chunk(nullptr, kHidConfigUsbReportBytes));
  TEST_ASSERT_FALSE(enc.next_chunk(small_out, sizeof(small_out)));
}

void test_reassembler_error_paths_and_recovery() {
  FixedHidConfigReassembler<64> reassembler;
  HidConfigMessageView view{};
  uint8_t chunk[kHidConfigUsbReportBytes]{};
  HidConfigChunkEncoder enc;

  // Prepare a multi-chunk 120-byte payload (>63-8=55 max payload per USB chunk).
  std::string long_msg(120U, 'A');
  TEST_ASSERT_TRUE(enc.begin(
      HidConfigMsgType::Command,
      5U,
      reinterpret_cast<const uint8_t*>(long_msg.data()),
      long_msg.size(),
      kHidConfigUsbReportBytes));
  std::vector<std::vector<uint8_t>> chunks;
  while (enc.has_next()) {
    std::vector<uint8_t> c(kHidConfigUsbReportBytes);
    TEST_ASSERT_TRUE(enc.next_chunk(c.data(), c.size()));
    chunks.push_back(c);
  }
  TEST_ASSERT_EQUAL_UINT32(3U, static_cast<uint32_t>(chunks.size()));

  // Null or too short
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::InvalidChunk,
                    reassembler.push_chunk(nullptr, kHidConfigUsbReportBytes, view));
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::InvalidChunk,
                    reassembler.push_chunk(chunk, kHidConfigChunkOverheadBytes - 1U, view));

  // Bad magic
  std::memcpy(chunk, chunks[0].data(), kHidConfigUsbReportBytes);
  chunk[0] = 0x00U;
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::InvalidChunk,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));

  // Unsupported version
  std::memcpy(chunk, chunks[0].data(), kHidConfigUsbReportBytes);
  chunk[1] = 2U;
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::UnsupportedVersion,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));

  // Reserved flags bit set
  std::memcpy(chunk, chunks[0].data(), kHidConfigUsbReportBytes);
  chunk[2] |= 0x40U;
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::InvalidChunk,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));

  // Invalid msg type (0)
  std::memcpy(chunk, chunks[0].data(), kHidConfigUsbReportBytes);
  chunk[2] = kHidConfigFlagStart;
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::InvalidChunk,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));

  // payload_len out of bounds
  std::memcpy(chunk, chunks[0].data(), kHidConfigUsbReportBytes);
  chunk[5] = static_cast<uint8_t>(kHidConfigUsbReportBytes);
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::InvalidChunk,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));

  // 1-bit CRC corruption in header & payload
  std::memcpy(chunk, chunks[0].data(), kHidConfigUsbReportBytes);
  chunk[3] ^= 0x01U;
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::BadCrc,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));
  std::memcpy(chunk, chunks[0].data(), kHidConfigUsbReportBytes);
  chunk[kHidConfigHeaderBytes] ^= 0x01U;
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::BadCrc,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));

  // Start flag with non-zero chunk_idx (valid CRC) -> SequenceError
  std::memcpy(chunk, chunks[1].data(), kHidConfigUsbReportBytes);
  chunk[2] |= kHidConfigFlagStart;
  const uint16_t start_bad_idx_crc =
      tandem::crc16_ccitt_false(chunk, kHidConfigHeaderBytes + chunk[5]);
  chunk[kHidConfigHeaderBytes + chunk[5]] = static_cast<uint8_t>(start_bad_idx_crc & 0xFFU);
  chunk[kHidConfigHeaderBytes + chunk[5] + 1U] = static_cast<uint8_t>((start_bad_idx_crc >> 8U) & 0xFFU);
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::SequenceError,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));

  // Continuation chunk without active message -> SequenceError
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::SequenceError,
                    reassembler.push_chunk(chunks[1].data(), chunks[1].size(), view));

  // Missing middle chunk: chunk 0 then chunk 2 -> SequenceError
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::Incomplete,
                    reassembler.push_chunk(chunks[0].data(), chunks[0].size(), view));
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::SequenceError,
                    reassembler.push_chunk(chunks[2].data(), chunks[2].size(), view));

  // Overflow: capacity is 64 (max 63 chars), chunk 0 (55) + chunk 1 (55) = 110 > 63
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::Incomplete,
                    reassembler.push_chunk(chunks[0].data(), chunks[0].size(), view));
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::Overflow,
                    reassembler.push_chunk(chunks[1].data(), chunks[1].size(), view));

  // Clean recovery on next valid Start chunk
  const std::string ok_cmd = "$TANDEM get\n";
  TEST_ASSERT_TRUE(enc.begin(
      HidConfigMsgType::Command,
      11U,
      reinterpret_cast<const uint8_t*>(ok_cmd.data()),
      ok_cmd.size(),
      kHidConfigUsbReportBytes));
  TEST_ASSERT_TRUE(enc.next_chunk(chunk, sizeof(chunk)));
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::MessageReady,
                    reassembler.push_chunk(chunk, sizeof(chunk), view));
  TEST_ASSERT_EQUAL_UINT8(11U, view.seq);
  TEST_ASSERT_EQUAL_STRING(ok_cmd.c_str(), view.data);
}

void test_tx_queue_priority_retries_and_ble_sizing() {
  HidConfigTxQueue tx(kHidConfigUsbReportBytes);
  TEST_ASSERT_FALSE(tx.has_pending());

  const std::string live1 = "@TANDEM {\"v\":1,\"cmd\":\"live\",\"raw\":[1]}\n";
  const std::string live2 = "@TANDEM {\"v\":1,\"cmd\":\"live\",\"raw\":[2]}\n";
  const std::string resp1 = "@TANDEM {\"v\":1,\"cmd\":\"caps\",\"ok\":true}\n";
  const std::string resp2 = "@TANDEM {\"v\":1,\"cmd\":\"save\",\"ok\":true}\n";

  // Enqueue live, then newer live (coalesces), plus response (prioritized over live).
  TEST_ASSERT_TRUE(tx.enqueue_live(live1.data(), live1.size()));
  TEST_ASSERT_TRUE(tx.enqueue_live(live2.data(), live2.size()));
  TEST_ASSERT_TRUE(tx.enqueue_response(17U, resp1.data(), resp1.size()));
  TEST_ASSERT_TRUE(tx.has_pending());

  // First peek should belong to resp1 and stay idempotent across retries before consume.
  const uint8_t* ptr1 = nullptr;
  const uint8_t* ptr2 = nullptr;
  size_t len1 = 0;
  size_t len2 = 0;
  TEST_ASSERT_TRUE(tx.peek_chunk(ptr1, len1));
  TEST_ASSERT_TRUE(tx.peek_chunk(ptr2, len2));
  TEST_ASSERT_EQUAL_UINT32(kHidConfigUsbReportBytes, static_cast<uint32_t>(len1));
  TEST_ASSERT_EQUAL_PTR(ptr1, ptr2);
  TEST_ASSERT_EQUAL_INT(0, std::memcmp(ptr1, ptr2, len1));

  // Enqueue back-to-back response while resp1 chunk is in-flight.
  TEST_ASSERT_TRUE(tx.enqueue_response(18U, resp2.data(), resp2.size()));

  FixedHidConfigReassembler<kMaxResponseLength> rx;
  HidConfigMessageView view{};

  // Consume resp1 (now active), then resp2, then live2.
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::MessageReady,
                    rx.push_chunk(ptr1, len1, view));
  TEST_ASSERT_EQUAL(HidConfigMsgType::Response, view.type);
  TEST_ASSERT_EQUAL_UINT8(17U, view.seq);
  TEST_ASSERT_EQUAL_STRING(resp1.c_str(), view.data);
  tx.consume_chunk();

  // Next must be resp2 before coalesced live2.
  TEST_ASSERT_TRUE(tx.peek_chunk(ptr1, len1));
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::MessageReady,
                    rx.push_chunk(ptr1, len1, view));
  TEST_ASSERT_EQUAL(HidConfigMsgType::Response, view.type);
  TEST_ASSERT_EQUAL_UINT8(18U, view.seq);
  TEST_ASSERT_EQUAL_STRING(resp2.c_str(), view.data);
  tx.consume_chunk();

  // Finally coalesced live2.
  TEST_ASSERT_TRUE(tx.peek_chunk(ptr1, len1));
  TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::MessageReady,
                    rx.push_chunk(ptr1, len1, view));
  TEST_ASSERT_EQUAL(HidConfigMsgType::Live, view.type);
  TEST_ASSERT_EQUAL_STRING(live2.c_str(), view.data);
  tx.consume_chunk();

  TEST_ASSERT_FALSE(tx.has_pending());
  TEST_ASSERT_FALSE(tx.peek_chunk(ptr1, len1));

  // Verify BLE 20-byte multi-chunk TX queue drain + reset().
  TEST_ASSERT_TRUE(tx.set_report_bytes(kHidConfigBleReportBytes));
  TEST_ASSERT_TRUE(tx.enqueue_response(25U, resp1.data(), resp1.size()));
  bool got_msg = false;
  while (tx.peek_chunk(ptr1, len1)) {
    TEST_ASSERT_EQUAL_UINT32(kHidConfigBleReportBytes, static_cast<uint32_t>(len1));
    const auto res = rx.push_chunk(ptr1, len1, view);
    tx.consume_chunk();
    if (res == HidConfigReassembler::FeedResult::MessageReady) {
      got_msg = true;
    } else {
      TEST_ASSERT_EQUAL(HidConfigReassembler::FeedResult::Incomplete, res);
    }
  }
  TEST_ASSERT_TRUE(got_msg);
  TEST_ASSERT_EQUAL_UINT8(25U, view.seq);
  TEST_ASSERT_EQUAL_STRING(resp1.c_str(), view.data);

  TEST_ASSERT_TRUE(tx.enqueue_live(live1.data(), live1.size()));
  TEST_ASSERT_TRUE(tx.has_pending());
  tx.reset();
  TEST_ASSERT_FALSE(tx.has_pending());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_descriptor_builder_usb_and_ble);
  RUN_TEST(test_round_trip_single_zero_and_multichunk_usb_and_ble);
  RUN_TEST(test_encoder_validation_guards);
  RUN_TEST(test_reassembler_error_paths_and_recovery);
  RUN_TEST(test_tx_queue_priority_retries_and_ble_sizing);
  return UNITY_END();
}

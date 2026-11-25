#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "tandem/ble_config_gatt.h"

using tandem::config::BleLineAssembler;

void setUp(void) {}
void tearDown(void) {}

void test_assembler_handles_chunked_line(void) {
  BleLineAssembler assembler;
  char out[tandem::config::kMaxCommandLength]{};
  const char* msg = "$TANDEM caps\r\n";
  const size_t len = std::strlen(msg);

  for (size_t i = 0; i + 1 < len; ++i) {
    TEST_ASSERT_EQUAL(BleLineAssembler::FeedResult::Incomplete, assembler.push_byte(static_cast<uint8_t>(msg[i]), out, sizeof(out)));
  }
  TEST_ASSERT_EQUAL(BleLineAssembler::FeedResult::LineReady, assembler.push_byte(static_cast<uint8_t>(msg[len - 1]), out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("$TANDEM caps", out);
}

void test_assembler_recovers_after_overflow(void) {
  BleLineAssembler assembler;
  char out[tandem::config::kMaxCommandLength]{};

  for (size_t i = 0; i < tandem::config::kMaxCommandLength + 10; ++i) {
    TEST_ASSERT_EQUAL(BleLineAssembler::FeedResult::Incomplete, assembler.push_byte('A', out, sizeof(out)));
  }
  TEST_ASSERT_EQUAL(BleLineAssembler::FeedResult::Overflow, assembler.push_byte('\n', out, sizeof(out)));

  const char* next = "$TANDEM get\n";
  BleLineAssembler::FeedResult last = BleLineAssembler::FeedResult::Incomplete;
  for (size_t i = 0; next[i] != '\0'; ++i) {
    last = assembler.push_byte(static_cast<uint8_t>(next[i]), out, sizeof(out));
  }
  TEST_ASSERT_EQUAL(BleLineAssembler::FeedResult::LineReady, last);
  TEST_ASSERT_EQUAL_STRING("$TANDEM get", out);
}

void test_clamp_ble_chunk_bytes(void) {
  TEST_ASSERT_EQUAL_UINT32(20, tandem::config::clamp_ble_chunk_bytes(0));
  TEST_ASSERT_EQUAL_UINT32(20, tandem::config::clamp_ble_chunk_bytes(23));
  TEST_ASSERT_EQUAL_UINT32(182, tandem::config::clamp_ble_chunk_bytes(185));
  TEST_ASSERT_EQUAL_UINT32(240, tandem::config::clamp_ble_chunk_bytes(512));
}

void test_for_each_ble_chunk_reassembles_exact(void) {
  const std::string payload = "@TANDEM {\"v\":1,\"cmd\":\"get\",\"ok\":true}\n";
  std::string reassembled;
  std::vector<size_t> sizes;

  tandem::config::for_each_ble_chunk(payload.data(), payload.size(), 20, [&](const uint8_t* ptr, size_t slice_len) {
    sizes.push_back(slice_len);
    reassembled.append(reinterpret_cast<const char*>(ptr), slice_len);
  });

  TEST_ASSERT_EQUAL_UINT32(2, sizes.size());
  TEST_ASSERT_EQUAL_UINT32(20, sizes[0]);
  TEST_ASSERT_EQUAL_UINT32(payload.size() - 20, sizes[1]);
  TEST_ASSERT_EQUAL_STRING(payload.c_str(), reassembled.c_str());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_assembler_handles_chunked_line);
  RUN_TEST(test_assembler_recovers_after_overflow);
  RUN_TEST(test_clamp_ble_chunk_bytes);
  RUN_TEST(test_for_each_ble_chunk_reassembles_exact);
  return UNITY_END();
}

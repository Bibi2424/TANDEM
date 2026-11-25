#include <unity.h>

#include <cstdint>
#include <cstring>

#include "tandem/device_name.h"

void setUp() {}
void tearDown() {}

void test_formats_mac_suffix_uppercase() {
  const uint8_t mac[6] = {0x48, 0x27, 0xE2, 0x4C, 0x0A, 0xF9};
  char buf[32]{};
  const size_t len = tandem::format_device_name(buf, sizeof(buf), "TANDEM-BLE", mac);
  TEST_ASSERT_EQUAL_STRING("TANDEM-BLE-0AF9", buf);
  TEST_ASSERT_EQUAL_UINT32(15U, len);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(buf), len);
}

void test_formats_usb_name_within_bounds() {
  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0xAB, 0xCD};
  char buf[32]{};
  const size_t len = tandem::format_device_name(buf, sizeof(buf), "TANDEM-Stick", mac);
  TEST_ASSERT_EQUAL_STRING("TANDEM-Stick-ABCD", buf);
  TEST_ASSERT_EQUAL_UINT32(17U, len);
  TEST_ASSERT_TRUE(len < 31U);
}

void test_truncates_safely_on_small_buffer() {
  const uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x12, 0x34};
  char buf[10]{};
  const size_t len = tandem::format_device_name(buf, sizeof(buf), "TANDEM-BLE", mac);
  TEST_ASSERT_EQUAL_STRING("TANDEM-BL", buf);
  TEST_ASSERT_EQUAL_UINT32(9U, len);
  TEST_ASSERT_EQUAL_CHAR('\0', buf[9]);
}

void test_handles_null_and_zero_len() {
  const uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x12, 0x34};
  char buf[8] = {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};

  TEST_ASSERT_EQUAL_UINT32(0U, tandem::format_device_name(nullptr, 8, "A", mac));
  TEST_ASSERT_EQUAL_UINT32(0U, tandem::format_device_name(buf, 0, "A", mac));
  TEST_ASSERT_EQUAL_UINT32(0U, tandem::format_device_name(buf, sizeof(buf), nullptr, mac));
  TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);

  buf[0] = 'Y';
  TEST_ASSERT_EQUAL_UINT32(0U, tandem::format_device_name(buf, sizeof(buf), "A", nullptr));
  TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_formats_mac_suffix_uppercase);
  RUN_TEST(test_formats_usb_name_within_bounds);
  RUN_TEST(test_truncates_safely_on_small_buffer);
  RUN_TEST(test_handles_null_and_zero_len);
  return UNITY_END();
}

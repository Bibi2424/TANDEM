// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstdint>

#include "tandem/hid_report.h"

static_assert(__cplusplus >= 201703L, "Native tests expect C++17 or newer");

void setUp() {}
void tearDown() {}

void test_descriptor_header_and_size() {
  TEST_ASSERT_EQUAL_UINT8(1U, tandem::kHidReportIdJoystick);
  TEST_ASSERT_EQUAL_UINT32(21U, static_cast<uint32_t>(tandem::kHidJoystickReportBytes));
  TEST_ASSERT_EQUAL_UINT32(82U, static_cast<uint32_t>(tandem::kHidJoystickReportDescriptorBytes));

  // Usage Page (Generic Desktop), Usage (Joystick), Collection (Application), Report ID (1)
  TEST_ASSERT_EQUAL_HEX8(0x05U, tandem::kHidJoystickReportDescriptor[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01U, tandem::kHidJoystickReportDescriptor[1]);
  TEST_ASSERT_EQUAL_HEX8(0x09U, tandem::kHidJoystickReportDescriptor[2]);
  TEST_ASSERT_EQUAL_HEX8(0x04U, tandem::kHidJoystickReportDescriptor[3]);
  TEST_ASSERT_EQUAL_HEX8(0xA1U, tandem::kHidJoystickReportDescriptor[4]);
  TEST_ASSERT_EQUAL_HEX8(0x01U, tandem::kHidJoystickReportDescriptor[5]);
  TEST_ASSERT_EQUAL_HEX8(0x85U, tandem::kHidJoystickReportDescriptor[6]);
  TEST_ASSERT_EQUAL_HEX8(tandem::kHidReportIdJoystick, tandem::kHidJoystickReportDescriptor[7]);

  // End Collection
  TEST_ASSERT_EQUAL_HEX8(0xC0U, tandem::kHidJoystickReportDescriptor[tandem::kHidJoystickReportDescriptorBytes - 1]);
}

void test_encode_centered_state_is_all_zeros() {
  const tandem::StickState state = tandem::make_centered_state(tandem::kStateFlagConnected);
  const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);

  for (size_t i = 0; i < tandem::kHidJoystickReportBytes; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0x00U, report.bytes[i]);
  }
}

void test_encode_axes_buttons_and_hat_little_endian() {
  tandem::StickState state{};
  state.axes[0] = 0x1234;            // LE: 0x34, 0x12
  state.axes[1] = -0x1234;           // -4660 = 0xEDCC -> LE: 0xCC, 0xED
  state.axes[2] = tandem::kAxisMin;  // -32767 = 0x8001 -> LE: 0x01, 0x80
  state.axes[3] = tandem::kAxisMax;  // +32767 = 0x7FFF -> LE: 0xFF, 0x7F
  state.axes[4] = 1;
  state.axes[5] = -1;
  state.axes[6] = 16384;
  state.axes[7] = -16384;
  state.buttons = 0x89ABCDEFU;  // LE: EF, CD, AB, 89
  state.hat = 5U;               // South

  const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);

  // Axes (16 bytes)
  TEST_ASSERT_EQUAL_HEX8(0x34U, report.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x12U, report.bytes[1]);
  TEST_ASSERT_EQUAL_HEX8(0xCCU, report.bytes[2]);
  TEST_ASSERT_EQUAL_HEX8(0xEDU, report.bytes[3]);
  TEST_ASSERT_EQUAL_HEX8(0x01U, report.bytes[4]);
  TEST_ASSERT_EQUAL_HEX8(0x80U, report.bytes[5]);
  TEST_ASSERT_EQUAL_HEX8(0xFFU, report.bytes[6]);
  TEST_ASSERT_EQUAL_HEX8(0x7FU, report.bytes[7]);
  TEST_ASSERT_EQUAL_HEX8(0x01U, report.bytes[8]);
  TEST_ASSERT_EQUAL_HEX8(0x00U, report.bytes[9]);
  TEST_ASSERT_EQUAL_HEX8(0xFFU, report.bytes[10]);
  TEST_ASSERT_EQUAL_HEX8(0xFFU, report.bytes[11]);
  TEST_ASSERT_EQUAL_HEX8(0x00U, report.bytes[12]);
  TEST_ASSERT_EQUAL_HEX8(0x40U, report.bytes[13]);
  TEST_ASSERT_EQUAL_HEX8(0x00U, report.bytes[14]);
  TEST_ASSERT_EQUAL_HEX8(0xC0U, report.bytes[15]);

  // Buttons (4 bytes)
  TEST_ASSERT_EQUAL_HEX8(0xEFU, report.bytes[16]);
  TEST_ASSERT_EQUAL_HEX8(0xCDU, report.bytes[17]);
  TEST_ASSERT_EQUAL_HEX8(0xABU, report.bytes[18]);
  TEST_ASSERT_EQUAL_HEX8(0x89U, report.bytes[19]);

  // Hat (1 byte: lower 4 bits = 5, upper 4 bits = 0)
  TEST_ASSERT_EQUAL_HEX8(0x05U, report.bytes[20]);
}

void test_encode_clamps_int16_min_and_invalid_hat() {
  tandem::StickState state{};
  state.axes[0] = INT16_MIN;  // -32768 must clamp to kAxisMin (-32767 = 0x8001)
  state.hat = 9U;             // > 8 must clamp to kHatCentered (0)

  tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
  TEST_ASSERT_EQUAL_HEX8(0x01U, report.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x80U, report.bytes[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00U, report.bytes[20]);

  state.hat = 255U;
  report = tandem::encode_hid_joystick_report(state);
  TEST_ASSERT_EQUAL_HEX8(0x00U, report.bytes[20]);

  // Valid hat boundary values 0..8 pass through unchanged
  for (uint8_t h = 0; h <= 8; ++h) {
    state.hat = h;
    report = tandem::encode_hid_joystick_report(state);
    TEST_ASSERT_EQUAL_HEX8(h, report.bytes[20]);
  }
}

void test_encode_buffer_guards() {
  const tandem::StickState state{};
  uint8_t small_buf[tandem::kHidJoystickReportBytes - 1]{};
  uint8_t exact_buf[tandem::kHidJoystickReportBytes]{};

  TEST_ASSERT_FALSE(tandem::encode_hid_joystick_report(state, nullptr, tandem::kHidJoystickReportBytes));
  TEST_ASSERT_FALSE(tandem::encode_hid_joystick_report(state, small_buf, sizeof(small_buf)));
  TEST_ASSERT_TRUE(tandem::encode_hid_joystick_report(state, exact_buf, sizeof(exact_buf)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_descriptor_header_and_size);
  RUN_TEST(test_encode_centered_state_is_all_zeros);
  RUN_TEST(test_encode_axes_buttons_and_hat_little_endian);
  RUN_TEST(test_encode_clamps_int16_min_and_invalid_hat);
  RUN_TEST(test_encode_buffer_guards);
  return UNITY_END();
}

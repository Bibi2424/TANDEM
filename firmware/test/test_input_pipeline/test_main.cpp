// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstdint>

#include "tandem/input_pipeline.h"

static_assert(__cplusplus >= 201703L, "Native tests expect C++17 or newer");

void setUp() {}
void tearDown() {}

void test_default_config_forces_all_axes_to_center() {
  tandem::RawStickInput raw{};
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    raw.axes[i] = static_cast<int32_t>(i) * 500 + 100;
  }
  raw.buttons = 0xDEADBEEFU;
  raw.hat = 3U;

  const tandem::StickInputConfig config{};
  const tandem::StickState state = tandem::process_stick_input(raw, config);

  TEST_ASSERT_EQUAL_UINT32(24U, sizeof(state));
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFU, state.buttons);
  TEST_ASSERT_EQUAL_UINT8(3U, state.hat);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, state.flags);
  TEST_ASSERT_EQUAL_UINT8(0U, state.reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, state.reserved[1]);
}

void test_custom_flags_and_zeroed_reserved() {
  const tandem::RawStickInput raw{};
  const tandem::StickInputConfig config{};

  const tandem::StickState none_state = tandem::process_stick_input(raw, config, tandem::kStateFlagNone);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagNone, none_state.flags);
  TEST_ASSERT_EQUAL_UINT8(0U, none_state.reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, none_state.reserved[1]);

  const uint8_t custom_flags = tandem::kStateFlagConnected | tandem::kStateFlagFailsafe;
  const tandem::StickState combined_state = tandem::process_stick_input(raw, config, custom_flags);
  TEST_ASSERT_EQUAL_UINT8(custom_flags, combined_state.flags);
  TEST_ASSERT_EQUAL_UINT8(0U, combined_state.reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, combined_state.reserved[1]);
}

void test_non_contiguous_multi_axis_stick_config() {
  tandem::StickInputConfig config{};

  // Slot 0: X axis, symmetric, 20% deadzone, 50% expo
  config.axes[0].calibration = {0, 1000, 2000};
  config.axes[0].deadzone = 0.2f;
  config.axes[0].curve = tandem::make_expo_curve(0.5f);
  config.axes[0].enabled = true;

  // Slot 1: Y axis, inverted, asymmetric
  config.axes[1].calibration = {100, 1100, 4100};
  config.axes[1].invert = true;
  config.axes[1].enabled = true;

  // Slot 5: Rz twist axis, linear full deflection
  config.axes[5].calibration = {0, 2048, 4095};
  config.axes[5].enabled = true;

  // Slot 6: Slider inside deadzone -> centered
  config.axes[6].calibration = {0, 2000, 4000};
  config.axes[6].deadzone = 0.25f;
  config.axes[6].enabled = true;

  tandem::RawStickInput raw{};
  raw.axes[0] = 1600;       // -> +10240
  raw.axes[1] = 600;        // -> +16384 (cal = -0.5, inverted = +0.5)
  raw.axes[2] = INT32_MAX;  // disabled -> 0
  raw.axes[3] = INT32_MIN;  // disabled -> 0
  raw.axes[4] = 4095;       // disabled -> 0
  raw.axes[5] = 4095;       // -> +32767
  raw.axes[6] = 2400;       // +0.2 <= dz 0.25 -> 0
  raw.axes[7] = -12345;     // disabled -> 0
  raw.buttons = 0x0005A5A5U;
  raw.hat = 7U;

  const tandem::StickState state = tandem::process_stick_input(raw, config);

  TEST_ASSERT_EQUAL_INT16(10240, state.axes[0]);
  TEST_ASSERT_EQUAL_INT16(16384, state.axes[1]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[2]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[3]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[4]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, state.axes[5]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[6]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[7]);
  TEST_ASSERT_EQUAL_UINT32(0x0005A5A5U, state.buttons);
  TEST_ASSERT_EQUAL_UINT8(7U, state.hat);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, state.flags);
  TEST_ASSERT_EQUAL_UINT8(0U, state.reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, state.reserved[1]);
}

void test_all_axes_enabled_bounds_and_degenerate_slot() {
  tandem::StickInputConfig config{};
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    config.axes[i].enabled = true;
  }
  config.axes[0].calibration = {0, 2048, 4095};
  config.axes[1].calibration = {INT32_MIN, 0, INT32_MAX};
  config.axes[2].calibration = {100, 100, 200};  // degenerate: min == center

  tandem::RawStickInput raw{};
  raw.axes[0] = 0;
  raw.axes[1] = INT32_MIN;
  raw.axes[2] = 150;
  raw.axes[3] = INT32_MAX;
  raw.axes[4] = 2048;
  raw.axes[5] = -1000;
  raw.axes[6] = 10000;
  raw.axes[7] = 4095;

  const tandem::StickState state = tandem::process_stick_input(raw, config);

  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, state.axes[0]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, state.axes[1]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[2]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, state.axes[3]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, state.axes[4]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, state.axes[5]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, state.axes[6]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, state.axes[7]);

  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    TEST_ASSERT_TRUE(state.axes[i] >= tandem::kAxisMin && state.axes[i] <= tandem::kAxisMax);
    TEST_ASSERT_NOT_EQUAL(INT16_MIN, state.axes[i]);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_forces_all_axes_to_center);
  RUN_TEST(test_custom_flags_and_zeroed_reserved);
  RUN_TEST(test_non_contiguous_multi_axis_stick_config);
  RUN_TEST(test_all_axes_enabled_bounds_and_degenerate_slot);
  return UNITY_END();
}

// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstdint>

#include "tandem/stick_combiner.h"

static_assert(__cplusplus >= 201703L, "Native tests expect C++17 or newer");

void setUp() {}
void tearDown() {}

void test_default_combine_config_hosas_mapping() {
  tandem::StickState left = tandem::make_centered_state(tandem::kStateFlagConnected);
  left.axes[0] = 1000;
  left.axes[1] = -2000;
  left.axes[2] = 3000;
  left.axes[3] = -4000;
  left.axes[4] = 9999;  // Unmapped in default 4+4 config
  left.buttons = 0xFFFF1234U;
  left.hat = 3U;

  tandem::StickState right = tandem::make_centered_state(tandem::kStateFlagConnected);
  right.axes[0] = -1111;
  right.axes[1] = 2222;
  right.axes[2] = -3333;
  right.axes[3] = 4444;
  right.buttons = 0xFFFFA55AU;
  right.hat = 7U;

  const tandem::StickState combined = tandem::combine_sticks(left, right);

  TEST_ASSERT_EQUAL_INT16(1000, combined.axes[0]);
  TEST_ASSERT_EQUAL_INT16(-2000, combined.axes[1]);
  TEST_ASSERT_EQUAL_INT16(3000, combined.axes[2]);
  TEST_ASSERT_EQUAL_INT16(-4000, combined.axes[3]);
  TEST_ASSERT_EQUAL_INT16(-1111, combined.axes[4]);
  TEST_ASSERT_EQUAL_INT16(2222, combined.axes[5]);
  TEST_ASSERT_EQUAL_INT16(-3333, combined.axes[6]);
  TEST_ASSERT_EQUAL_INT16(4444, combined.axes[7]);
  TEST_ASSERT_EQUAL_HEX32(0xA55A1234U, combined.buttons);
  TEST_ASSERT_EQUAL_UINT8(3U, combined.hat);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, combined.flags);
  TEST_ASSERT_EQUAL_UINT8(0U, combined.reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, combined.reserved[1]);
}

void test_custom_any_to_any_axis_and_button_remapping() {
  tandem::StickState joy1 = tandem::make_centered_state(tandem::kStateFlagConnected);
  joy1.axes[0] = 111;                  // 1.X
  joy1.axes[1] = 222;                  // 1.Y
  joy1.axes[2] = 333;                  // 1.Z
  joy1.buttons = (uint32_t{1} << 20);  // Button 20 on Stick 1

  tandem::StickState joy2 = tandem::make_centered_state(tandem::kStateFlagConnected);
  joy2.axes[0] = -444;                // 2.X
  joy2.axes[1] = -555;                // 2.Y
  joy2.axes[2] = -666;                // 2.Z
  joy2.buttons = (uint32_t{1} << 0);  // Button 0 on Stick 2

  // Remap: 1.X->Y(1), 1.Y->Z(2), 1.Z->Rx(3), 2.X->X(0), 2.Y->Rz(5), 2.Z->Ry(4)
  // Slots 6 (Slider) & 7 (Dial) left unmapped (Unknown -> 0).
  tandem::StickCombineConfig cfg{};
  cfg.axis_map[0] = {tandem::StickId::Right, 0};  // 2.X -> X
  cfg.axis_map[1] = {tandem::StickId::Left, 0};   // 1.X -> Y
  cfg.axis_map[2] = {tandem::StickId::Left, 1};   // 1.Y -> Z
  cfg.axis_map[3] = {tandem::StickId::Left, 2};   // 1.Z -> Rx
  cfg.axis_map[4] = {tandem::StickId::Right, 2};  // 2.Z -> Ry
  cfg.axis_map[5] = {tandem::StickId::Right, 1};  // 2.Y -> Rz

  // Cross-map buttons: out[0] <- joy1 btn 20, out[31] <- joy2 btn 0
  cfg.button_map[0] = {tandem::StickId::Left, 20};
  cfg.button_map[31] = {tandem::StickId::Right, 0};

  const tandem::StickState out = tandem::combine_sticks(joy1, joy2, cfg);

  TEST_ASSERT_EQUAL_INT16(-444, out.axes[0]);
  TEST_ASSERT_EQUAL_INT16(111, out.axes[1]);
  TEST_ASSERT_EQUAL_INT16(222, out.axes[2]);
  TEST_ASSERT_EQUAL_INT16(333, out.axes[3]);
  TEST_ASSERT_EQUAL_INT16(-666, out.axes[4]);
  TEST_ASSERT_EQUAL_INT16(-555, out.axes[5]);
  TEST_ASSERT_EQUAL_INT16(0, out.axes[6]);
  TEST_ASSERT_EQUAL_INT16(0, out.axes[7]);
  TEST_ASSERT_EQUAL_HEX32(0x80000001U, out.buttons);
}

void test_single_stick_failsafe_isolation() {
  const tandem::StickState left_fs = tandem::make_centered_state(tandem::kStateFlagFailsafe);
  tandem::StickState right = tandem::make_centered_state(tandem::kStateFlagConnected);
  right.axes[0] = 15000;
  right.axes[1] = -15000;
  right.buttons = 0x0000000FU;
  right.hat = 5U;

  const tandem::StickState combined = tandem::combine_sticks(left_fs, right);

  for (uint8_t i = 0; i < 4U; ++i) {
    TEST_ASSERT_EQUAL_INT16(0, combined.axes[i]);
  }
  TEST_ASSERT_EQUAL_INT16(15000, combined.axes[4]);
  TEST_ASSERT_EQUAL_INT16(-15000, combined.axes[5]);
  TEST_ASSERT_EQUAL_HEX32(0x000F0000U, combined.buttons);
  TEST_ASSERT_EQUAL_UINT8(5U, combined.hat);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected | tandem::kStateFlagFailsafe, combined.flags);
}

void test_hat_merge_policies_and_sanitization() {
  tandem::StickState left = tandem::make_centered_state();
  tandem::StickState right = tandem::make_centered_state();

  left.hat = 0U;
  right.hat = 6U;
  tandem::StickCombineConfig cfg = tandem::make_default_combine_config();
  cfg.hat_policy = tandem::HatMergePolicy::LeftThenRight;
  TEST_ASSERT_EQUAL_UINT8(6U, tandem::combine_sticks(left, right, cfg).hat);

  left.hat = 2U;
  TEST_ASSERT_EQUAL_UINT8(2U, tandem::combine_sticks(left, right, cfg).hat);

  cfg.hat_policy = tandem::HatMergePolicy::RightThenLeft;
  TEST_ASSERT_EQUAL_UINT8(6U, tandem::combine_sticks(left, right, cfg).hat);

  cfg.hat_policy = tandem::HatMergePolicy::LeftOnly;
  left.hat = 0U;
  TEST_ASSERT_EQUAL_UINT8(0U, tandem::combine_sticks(left, right, cfg).hat);

  cfg.hat_policy = tandem::HatMergePolicy::RightOnly;
  right.hat = 12U;  // Invalid > 8 -> sanitized to 0
  TEST_ASSERT_EQUAL_UINT8(0U, tandem::combine_sticks(left, right, cfg).hat);
}

void test_out_of_range_indices_and_int16_min_clamping() {
  tandem::StickState left = tandem::make_centered_state();
  left.axes[0] = INT16_MIN;
  left.buttons = 0xFFFFFFFFU;
  left.reserved[0] = 0xAAU;

  tandem::StickState right = tandem::make_centered_state();
  right.axes[0] = INT16_MIN;
  right.buttons = 0xFFFFFFFFU;
  right.reserved[1] = 0x55U;

  tandem::StickCombineConfig cfg{};
  cfg.axis_map[0] = {tandem::StickId::Left, 0};
  cfg.axis_map[1] = {tandem::StickId::Right, 0};
  cfg.axis_map[2] = {tandem::StickId::Left, 8};     // Out of range -> 0
  cfg.axis_map[3] = {tandem::StickId::Right, 99};   // Out of range -> 0
  cfg.button_map[0] = {tandem::StickId::Left, 32};  // Out of range -> 0
  cfg.button_map[1] = {tandem::StickId::Left, 0};   // Valid -> 1

  const tandem::StickState combined = tandem::combine_sticks(left, right, cfg);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, combined.axes[0]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, combined.axes[1]);
  TEST_ASSERT_EQUAL_INT16(0, combined.axes[2]);
  TEST_ASSERT_EQUAL_INT16(0, combined.axes[3]);
  TEST_ASSERT_EQUAL_HEX32(0x00000002U, combined.buttons);
  TEST_ASSERT_EQUAL_UINT8(0U, combined.reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, combined.reserved[1]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_combine_config_hosas_mapping);
  RUN_TEST(test_custom_any_to_any_axis_and_button_remapping);
  RUN_TEST(test_single_stick_failsafe_isolation);
  RUN_TEST(test_hat_merge_policies_and_sanitization);
  RUN_TEST(test_out_of_range_indices_and_int16_min_clamping);
  return UNITY_END();
}

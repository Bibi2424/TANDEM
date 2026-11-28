// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <climits>
#include <cstdint>
#include <initializer_list>

#include "tandem/synthetic_input.h"

namespace {

int count_bits(uint32_t value) {
  int count = 0;
  while (value != 0U) {
    count += static_cast<int>(value & 1U);
    value >>= 1U;
  }
  return count;
}

void test_synthetic_sets_connected_flag_and_valid_ranges() {
  const uint32_t timestamps[] = {0U, 250U, 500U, 1000U, 2000U, 3999U, 100000U};
  for (const uint32_t now_ms : timestamps) {
    for (const tandem::StickId id : {tandem::StickId::Left, tandem::StickId::Right}) {
      const tandem::StickState state = tandem::make_synthetic_stick_state(now_ms, id);
      TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, state.flags);
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(8U, state.hat);
      TEST_ASSERT_EQUAL_INT(1, count_bits(state.buttons));

      for (uint8_t axis = 0; axis < tandem::kMaxAxes; ++axis) {
        TEST_ASSERT_NOT_EQUAL(INT16_MIN, state.axes[axis]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT16(tandem::kAxisMin, state.axes[axis]);
        TEST_ASSERT_LESS_OR_EQUAL_INT16(tandem::kAxisMax, state.axes[axis]);
      }
    }
  }
}

void test_synthetic_left_and_right_are_distinct() {
  const tandem::StickState left = tandem::make_synthetic_stick_state(1234U, tandem::StickId::Left);
  const tandem::StickState right = tandem::make_synthetic_stick_state(1234U, tandem::StickId::Right);

  TEST_ASSERT_NOT_EQUAL_UINT32(left.buttons, right.buttons);
  bool any_axis_diff = false;
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    if (left.axes[i] != right.axes[i]) {
      any_axis_diff = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(any_axis_diff);
}

void test_synthetic_buttons_cycle_every_250ms() {
  TEST_ASSERT_EQUAL_UINT32(uint32_t{1} << 0, tandem::make_synthetic_stick_state(0U, tandem::StickId::Left).buttons);
  TEST_ASSERT_EQUAL_UINT32(uint32_t{1} << 1, tandem::make_synthetic_stick_state(250U, tandem::StickId::Left).buttons);
  TEST_ASSERT_EQUAL_UINT32(uint32_t{1} << 31, tandem::make_synthetic_stick_state(7750U, tandem::StickId::Left).buttons);
  TEST_ASSERT_EQUAL_UINT32(uint32_t{1} << 0, tandem::make_synthetic_stick_state(8000U, tandem::StickId::Left).buttons);

  TEST_ASSERT_EQUAL_UINT32(uint32_t{1} << 16, tandem::make_synthetic_stick_state(0U, tandem::StickId::Right).buttons);
}

void test_synthetic_hat_cycles_0_to_8_every_500ms() {
  TEST_ASSERT_EQUAL_UINT8(0U, tandem::make_synthetic_stick_state(0U).hat);
  TEST_ASSERT_EQUAL_UINT8(1U, tandem::make_synthetic_stick_state(500U).hat);
  TEST_ASSERT_EQUAL_UINT8(8U, tandem::make_synthetic_stick_state(4000U).hat);
  TEST_ASSERT_EQUAL_UINT8(0U, tandem::make_synthetic_stick_state(4500U).hat);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_synthetic_sets_connected_flag_and_valid_ranges);
  RUN_TEST(test_synthetic_left_and_right_are_distinct);
  RUN_TEST(test_synthetic_buttons_cycle_every_250ms);
  RUN_TEST(test_synthetic_hat_cycles_0_to_8_every_500ms);
  return UNITY_END();
}

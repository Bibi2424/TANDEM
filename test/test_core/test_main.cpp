#include <unity.h>

#include <cctype>
#include <cstdint>
#include <cstring>

#include "tandem/core_info.h"
#include "tandem/types.h"

static_assert(__cplusplus >= 201703L, "Native tests expect C++17 or newer");

void setUp() {}
void tearDown() {}

void test_core_version_is_defined() {
  TEST_ASSERT_NOT_NULL(tandem::core_version());
  TEST_ASSERT_EQUAL_STRING("0.1.0", tandem::core_version());
}

void test_core_git_commit_is_defined() {
  const char* commit = tandem::core_git_commit();
  TEST_ASSERT_NOT_NULL(commit);
  TEST_ASSERT_TRUE(std::strcmp(commit, "unknown") != 0);
  const std::size_t len = std::strlen(commit);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4U, static_cast<uint32_t>(len));
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(16U, static_cast<uint32_t>(len));
  for (std::size_t i = 0; i < len; ++i) {
    TEST_ASSERT_TRUE(std::isxdigit(static_cast<unsigned char>(commit[i])) != 0);
  }
}

void test_axis_bounds_are_symmetric() {
  TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(INT16_MIN + 1), tandem::kAxisMin);
  TEST_ASSERT_EQUAL_INT16(-tandem::kAxisMax, tandem::kAxisMin);
  TEST_ASSERT_EQUAL_INT16(0, tandem::kAxisCenter);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, tandem::kAxisMax);
}

void test_make_centered_state_defaults() {
  const tandem::StickState state = tandem::make_centered_state();
  TEST_ASSERT_EQUAL_UINT8(8U, tandem::kMaxAxes);
  TEST_ASSERT_EQUAL_UINT32(24U, sizeof(state));
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    TEST_ASSERT_EQUAL_INT16(0, state.axes[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(0U, state.buttons);
  TEST_ASSERT_EQUAL_UINT8(tandem::kHatCentered, state.hat);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagNone, state.flags);
}

void test_make_centered_state_preserves_flags() {
  const uint8_t flags = tandem::kStateFlagConnected | tandem::kStateFlagFailsafe;
  const tandem::StickState state = tandem::make_centered_state(flags);
  TEST_ASSERT_EQUAL_UINT8(flags, state.flags);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_version_is_defined);
  RUN_TEST(test_core_git_commit_is_defined);
  RUN_TEST(test_axis_bounds_are_symmetric);
  RUN_TEST(test_make_centered_state_defaults);
  RUN_TEST(test_make_centered_state_preserves_flags);
  return UNITY_END();
}

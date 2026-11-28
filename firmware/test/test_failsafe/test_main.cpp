// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cstdint>

#include "tandem/failsafe.h"

static_assert(__cplusplus >= 201703L, "Native tests expect C++17 or newer");

void setUp() {}
void tearDown() {}

void test_boot_fail_closed_at_t0() {
  tandem::FailsafeMonitor monitor;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::Disconnected), static_cast<uint8_t>(monitor.status()));
  TEST_ASSERT_FALSE(monitor.is_connected());
  TEST_ASSERT_TRUE(monitor.is_failsafe());
  TEST_ASSERT_FALSE(monitor.has_received_packet());
  TEST_ASSERT_EQUAL_UINT32(0U, monitor.last_packet_ms());
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagFailsafe, monitor.state().flags);
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[i]);
  }

  monitor.update(0U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::Disconnected), static_cast<uint8_t>(monitor.status()));
  TEST_ASSERT_TRUE(monitor.is_failsafe());
}

void test_exact_timeout_boundary() {
  tandem::FailsafeConfig cfg{};
  cfg.timeout_ms = 200U;
  tandem::FailsafeMonitor monitor(cfg);

  tandem::StickState active = tandem::make_centered_state();
  active.axes[0] = 12345;
  active.axes[1] = -23456;
  active.buttons = 0x00000005U;
  active.hat = 3U;

  monitor.on_packet(active, 1000U);
  TEST_ASSERT_TRUE(monitor.is_connected());
  TEST_ASSERT_FALSE(monitor.is_failsafe());
  TEST_ASSERT_TRUE(monitor.has_received_packet());
  TEST_ASSERT_EQUAL_UINT32(1000U, monitor.last_packet_ms());
  TEST_ASSERT_EQUAL_INT16(12345, monitor.state().axes[0]);
  TEST_ASSERT_EQUAL_INT16(-23456, monitor.state().axes[1]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, monitor.state().flags);

  // Exact timeout - 1: still connected.
  monitor.update(1199U);
  TEST_ASSERT_TRUE(monitor.is_connected());
  TEST_ASSERT_EQUAL_INT16(12345, monitor.state().axes[0]);

  // Exact timeout: transitions to TimedOut with failsafe state.
  monitor.update(1200U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::TimedOut), static_cast<uint8_t>(monitor.status()));
  TEST_ASSERT_FALSE(monitor.is_connected());
  TEST_ASSERT_TRUE(monitor.is_failsafe());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[0]);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[1]);
  TEST_ASSERT_EQUAL_UINT32(0U, monitor.state().buttons);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagFailsafe, monitor.state().flags);
}

void test_uint32_wraparound_no_false_timeout_and_exact_expiry() {
  tandem::FailsafeConfig cfg{};
  cfg.timeout_ms = 200U;
  tandem::FailsafeMonitor monitor(cfg);

  tandem::StickState active = tandem::make_centered_state();
  active.axes[0] = 20000;

  const uint32_t t_pkt = UINT32_MAX - 50U;
  monitor.on_packet(active, t_pkt);
  TEST_ASSERT_TRUE(monitor.is_connected());

  monitor.update(UINT32_MAX);  // +50 ms elapsed
  TEST_ASSERT_TRUE(monitor.is_connected());

  monitor.update(148U);  // +199 ms elapsed across wraparound
  TEST_ASSERT_TRUE(monitor.is_connected());
  TEST_ASSERT_EQUAL_INT16(20000, monitor.state().axes[0]);

  monitor.update(149U);  // +200 ms elapsed across wraparound
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::TimedOut), static_cast<uint8_t>(monitor.status()));
  TEST_ASSERT_TRUE(monitor.is_failsafe());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[0]);
}

void test_latched_anti_resurrection_on_full_wraparound() {
  tandem::FailsafeConfig cfg{};
  cfg.timeout_ms = 200U;
  tandem::FailsafeMonitor monitor(cfg);

  tandem::StickState active = tandem::make_centered_state();
  active.axes[0] = 30000;

  monitor.on_packet(active, 1000U);
  monitor.update(1200U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::TimedOut), static_cast<uint8_t>(monitor.status()));

  // Simulated 49.7-day wraparound back into [1000, 1199] must NOT resurrect Connected.
  monitor.update(1000U);
  monitor.update(1050U);
  monitor.update(1199U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::TimedOut), static_cast<uint8_t>(monitor.status()));
  TEST_ASSERT_FALSE(monitor.is_connected());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[0]);
}

void test_recovery_on_fresh_packet() {
  tandem::FailsafeMonitor monitor;

  tandem::StickState s1 = tandem::make_centered_state();
  s1.axes[0] = 10000;
  monitor.on_packet(s1, 100U);
  monitor.update(300U);
  TEST_ASSERT_TRUE(monitor.is_failsafe());

  tandem::StickState s2 = tandem::make_centered_state();
  s2.axes[0] = -15000;
  monitor.on_packet(s2, 5000U);
  TEST_ASSERT_TRUE(monitor.is_connected());
  TEST_ASSERT_EQUAL_INT16(-15000, monitor.state().axes[0]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, monitor.state().flags);
}

void test_trigger_failsafe_and_reset() {
  tandem::FailsafeMonitor monitor;

  // Before any packet: trigger_failsafe keeps Disconnected.
  monitor.trigger_failsafe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::Disconnected), static_cast<uint8_t>(monitor.status()));

  tandem::StickState active = tandem::make_centered_state();
  active.axes[2] = 9999;
  monitor.on_packet(active, 500U);
  TEST_ASSERT_TRUE(monitor.is_connected());

  monitor.trigger_failsafe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::TimedOut), static_cast<uint8_t>(monitor.status()));
  TEST_ASSERT_TRUE(monitor.is_failsafe());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[2]);

  monitor.reset();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::LinkStatus::Disconnected), static_cast<uint8_t>(monitor.status()));
  TEST_ASSERT_FALSE(monitor.has_received_packet());
  TEST_ASSERT_EQUAL_UINT32(0U, monitor.last_packet_ms());
  TEST_ASSERT_TRUE(monitor.is_failsafe());
}

void test_degenerate_zero_timeout_stays_fail_closed() {
  tandem::FailsafeConfig bad_cfg{};
  bad_cfg.timeout_ms = 0U;
  TEST_ASSERT_FALSE(tandem::is_valid(bad_cfg));

  tandem::FailsafeMonitor monitor(bad_cfg);
  tandem::StickState active = tandem::make_centered_state();
  active.axes[0] = 25000;

  monitor.on_packet(active, 100U);
  TEST_ASSERT_FALSE(monitor.is_connected());
  TEST_ASSERT_TRUE(monitor.is_failsafe());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[0]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagFailsafe, monitor.state().flags);

  // Switching an active monitor to timeout_ms == 0 must also fail closed immediately.
  tandem::FailsafeConfig good_cfg{};
  good_cfg.timeout_ms = 100U;
  monitor.set_config(good_cfg);
  monitor.on_packet(active, 200U);
  TEST_ASSERT_TRUE(monitor.is_connected());

  monitor.set_config(bad_cfg);
  TEST_ASSERT_FALSE(monitor.is_connected());
  TEST_ASSERT_TRUE(monitor.is_failsafe());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, monitor.state().axes[0]);
}

void test_custom_failsafe_state_and_int16_min_clamping() {
  tandem::FailsafeConfig cfg{};
  cfg.timeout_ms = 150U;
  cfg.failsafe_state = tandem::make_centered_state(tandem::kStateFlagConnected);
  cfg.failsafe_state.axes[3] = INT16_MIN;  // e.g. throttle at min, unsanitized
  cfg.failsafe_state.reserved[0] = 0xAAU;
  cfg.failsafe_state.reserved[1] = 0x55U;

  tandem::FailsafeMonitor monitor(cfg);
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, monitor.state().axes[3]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagFailsafe, monitor.state().flags);
  TEST_ASSERT_EQUAL_UINT8(0U, monitor.state().reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, monitor.state().reserved[1]);

  tandem::StickState active = tandem::make_centered_state(tandem::kStateFlagFailsafe);
  active.axes[0] = INT16_MIN;
  active.reserved[0] = 0x11U;
  active.reserved[1] = 0x22U;

  monitor.on_packet(active, 10U);
  TEST_ASSERT_TRUE(monitor.is_connected());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, monitor.state().axes[0]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kStateFlagConnected, monitor.state().flags);
  TEST_ASSERT_EQUAL_UINT8(0U, monitor.state().reserved[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, monitor.state().reserved[1]);
}

void test_independent_dual_stick_monitors() {
  tandem::FailsafeConfig cfg{};
  cfg.timeout_ms = 100U;
  tandem::FailsafeMonitor left(cfg);
  tandem::FailsafeMonitor right(cfg);

  tandem::StickState left_state = tandem::make_centered_state();
  left_state.axes[0] = 1111;
  tandem::StickState right_state = tandem::make_centered_state();
  right_state.axes[0] = -2222;

  left.on_packet(left_state, 1000U);
  right.on_packet(right_state, 1050U);

  left.update(1100U);
  right.update(1100U);
  TEST_ASSERT_TRUE(left.is_failsafe());
  TEST_ASSERT_TRUE(right.is_connected());
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, left.state().axes[0]);
  TEST_ASSERT_EQUAL_INT16(-2222, right.state().axes[0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_fail_closed_at_t0);
  RUN_TEST(test_exact_timeout_boundary);
  RUN_TEST(test_uint32_wraparound_no_false_timeout_and_exact_expiry);
  RUN_TEST(test_latched_anti_resurrection_on_full_wraparound);
  RUN_TEST(test_recovery_on_fresh_packet);
  RUN_TEST(test_trigger_failsafe_and_reset);
  RUN_TEST(test_degenerate_zero_timeout_stays_fail_closed);
  RUN_TEST(test_custom_failsafe_state_and_int16_min_clamping);
  RUN_TEST(test_independent_dual_stick_monitors);
  return UNITY_END();
}

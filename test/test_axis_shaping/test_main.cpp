#include <unity.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

#include "tandem/axis_shaping.h"

static_assert(__cplusplus >= 201703L, "Native tests expect C++17 or newer");

void setUp() {}
void tearDown() {}

namespace {

void assert_positive_zero(float value) {
  TEST_ASSERT_TRUE(value == 0.0f);
  TEST_ASSERT_FALSE(std::signbit(value));
}

}  // namespace

void test_defaults_validity_and_factories() {
  const tandem::AxisCalibration default_cal{};
  TEST_ASSERT_EQUAL_INT32(0, default_cal.min);
  TEST_ASSERT_EQUAL_INT32(2048, default_cal.center);
  TEST_ASSERT_EQUAL_INT32(4095, default_cal.max);
  TEST_ASSERT_TRUE(default_cal.is_valid());

  TEST_ASSERT_FALSE((tandem::AxisCalibration{0, 0, 100}.is_valid()));
  TEST_ASSERT_FALSE((tandem::AxisCalibration{0, 100, 100}.is_valid()));
  TEST_ASSERT_FALSE((tandem::AxisCalibration{100, 50, 200}.is_valid()));
  TEST_ASSERT_FALSE((tandem::AxisCalibration{0, 200, 100}.is_valid()));
  TEST_ASSERT_FALSE((tandem::AxisCalibration{200, 100, 0}.is_valid()));
  TEST_ASSERT_FALSE((tandem::AxisCalibration{100, 100, 100}.is_valid()));

  const tandem::AxisConfig default_cfg{};
  TEST_ASSERT_FALSE(default_cfg.enabled);
  TEST_ASSERT_FALSE(default_cfg.invert);
  TEST_ASSERT_TRUE(default_cfg.deadzone == 0.0f);
  TEST_ASSERT_EQUAL_UINT8(0U, static_cast<uint8_t>(tandem::CurveType::Linear));
  TEST_ASSERT_EQUAL_UINT8(1U, static_cast<uint8_t>(tandem::CurveType::Expo));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::CurveType::Linear), static_cast<uint8_t>(default_cfg.curve.type));

  const tandem::AxisCurve linear = tandem::make_linear_curve();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::CurveType::Linear), static_cast<uint8_t>(linear.type));
  TEST_ASSERT_TRUE(linear.param == 0.0f);

  const tandem::AxisCurve expo = tandem::make_expo_curve(0.4f);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::CurveType::Expo), static_cast<uint8_t>(expo.type));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.4f, expo.param);
}

void test_calibrate_axis_symmetric() {
  const tandem::AxisCalibration cal{0, 1000, 2000};

  assert_positive_zero(tandem::calibrate_axis(1000, cal));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(0, cal));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(2000, cal));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, tandem::calibrate_axis(500, cal));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, tandem::calibrate_axis(1500, cal));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(-500, cal));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(2500, cal));
}

void test_calibrate_axis_asymmetric() {
  const tandem::AxisCalibration asym{100, 1100, 4100};

  assert_positive_zero(tandem::calibrate_axis(1100, asym));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(100, asym));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(4100, asym));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, tandem::calibrate_axis(600, asym));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, tandem::calibrate_axis(2600, asym));

  const tandem::AxisCalibration adc12{};
  assert_positive_zero(tandem::calibrate_axis(2048, adc12));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(0, adc12));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(4095, adc12));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, tandem::calibrate_axis(1024, adc12));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1024.0f / 2047.0f, tandem::calibrate_axis(3072, adc12));
}

void test_calibrate_axis_degenerate_bounds() {
  const tandem::AxisCalibration invalid_cases[] = {
      {0, 0, 100}, {0, 100, 100}, {100, 50, 200}, {0, 200, 100}, {200, 100, 0}, {100, 100, 100},
  };
  const int32_t raw_inputs[] = {0, 100, -100, INT32_MIN, INT32_MAX};

  for (const auto& cal : invalid_cases) {
    for (int32_t raw : raw_inputs) {
      assert_positive_zero(tandem::calibrate_axis(raw, cal));
    }
  }
}

void test_calibrate_axis_int32_extremes() {
  const tandem::AxisCalibration full_range{INT32_MIN, 0, INT32_MAX};
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(INT32_MIN, full_range));
  assert_positive_zero(tandem::calibrate_axis(0, full_range));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(INT32_MAX, full_range));

  const float neg_mid = tandem::calibrate_axis(INT32_MIN / 2, full_range);
  const float pos_mid = tandem::calibrate_axis(INT32_MAX / 2, full_range);
  TEST_ASSERT_TRUE(std::isfinite(neg_mid) && neg_mid >= -1.0f && neg_mid < 0.0f);
  TEST_ASSERT_TRUE(std::isfinite(pos_mid) && pos_mid > 0.0f && pos_mid <= 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.5f, neg_mid);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, pos_mid);

  const tandem::AxisCalibration skew_low{INT32_MIN, INT32_MIN + 1, INT32_MAX};
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(INT32_MIN, skew_low));
  assert_positive_zero(tandem::calibrate_axis(INT32_MIN + 1, skew_low));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(INT32_MAX, skew_low));
  const float zero_in_skew_low = tandem::calibrate_axis(0, skew_low);
  TEST_ASSERT_TRUE(std::isfinite(zero_in_skew_low) && zero_in_skew_low > 0.0f && zero_in_skew_low <= 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, zero_in_skew_low);

  const tandem::AxisCalibration skew_high{INT32_MIN, INT32_MAX - 1, INT32_MAX};
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(INT32_MIN, skew_high));
  assert_positive_zero(tandem::calibrate_axis(INT32_MAX - 1, skew_high));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(INT32_MAX, skew_high));
  const float zero_in_skew_high = tandem::calibrate_axis(0, skew_high);
  TEST_ASSERT_TRUE(std::isfinite(zero_in_skew_high) && zero_in_skew_high >= -1.0f && zero_in_skew_high < 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, zero_in_skew_high);

  const tandem::AxisCalibration normal_adc{};
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::calibrate_axis(INT32_MIN, normal_adc));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::calibrate_axis(INT32_MAX, normal_adc));
}

void test_apply_deadzone_nominal_continuity_and_odd_symmetry() {
  constexpr float dz = 0.2f;

  assert_positive_zero(tandem::apply_deadzone(0.0f, dz));
  assert_positive_zero(tandem::apply_deadzone(-0.0f, dz));
  assert_positive_zero(tandem::apply_deadzone(0.1f, dz));
  assert_positive_zero(tandem::apply_deadzone(-0.1f, dz));
  assert_positive_zero(tandem::apply_deadzone(0.2f, dz));
  assert_positive_zero(tandem::apply_deadzone(-0.2f, dz));

  const float just_pos = tandem::apply_deadzone(0.2001f, dz);
  const float just_neg = tandem::apply_deadzone(-0.2001f, dz);
  TEST_ASSERT_TRUE(just_pos > 0.0f && just_pos < 0.001f);
  TEST_ASSERT_TRUE(just_neg < 0.0f && just_neg > -0.001f);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, tandem::apply_deadzone(0.6f, dz));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, tandem::apply_deadzone(-0.6f, dz));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::apply_deadzone(1.0f, dz));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::apply_deadzone(-1.0f, dz));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::apply_deadzone(1.5f, dz));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::apply_deadzone(-1.5f, dz));

  float prev = 0.0f;
  for (int i = 0; i <= 100; ++i) {
    const float x = static_cast<float>(i) / 100.0f;
    const float pos = tandem::apply_deadzone(x, dz);
    const float neg = tandem::apply_deadzone(-x, dz);
    TEST_ASSERT_TRUE(pos >= prev);
    TEST_ASSERT_TRUE(pos >= 0.0f && pos <= 1.0f);
    if (pos == 0.0f) {
      assert_positive_zero(pos);
      assert_positive_zero(neg);
    } else {
      TEST_ASSERT_TRUE(neg == -pos);
    }
    prev = pos;
  }
}

void test_apply_deadzone_guards_and_non_finite() {
  const float nan_val = std::numeric_limits<float>::quiet_NaN();
  const float inf_val = std::numeric_limits<float>::infinity();

  for (float dz : {0.0f, -0.25f}) {
    assert_positive_zero(tandem::apply_deadzone(-0.0f, dz));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, tandem::apply_deadzone(0.5f, dz));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, tandem::apply_deadzone(-0.5f, dz));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::apply_deadzone(1.5f, dz));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::apply_deadzone(-1.5f, dz));
  }

  for (float dz : {1.0f, 1.5f}) {
    assert_positive_zero(tandem::apply_deadzone(0.0f, dz));
    assert_positive_zero(tandem::apply_deadzone(0.5f, dz));
    assert_positive_zero(tandem::apply_deadzone(-0.5f, dz));
    assert_positive_zero(tandem::apply_deadzone(1.0f, dz));
    assert_positive_zero(tandem::apply_deadzone(-1.0f, dz));
  }

  const float almost_one = std::nextafter(1.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::apply_deadzone(1.0f, almost_one));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::apply_deadzone(-1.0f, almost_one));
  assert_positive_zero(tandem::apply_deadzone(0.5f, almost_one));
  assert_positive_zero(tandem::apply_deadzone(-0.5f, almost_one));

  for (float bad : {nan_val, inf_val, -inf_val}) {
    assert_positive_zero(tandem::apply_deadzone(bad, 0.1f));
    assert_positive_zero(tandem::apply_deadzone(0.5f, bad));
    assert_positive_zero(tandem::apply_deadzone(-0.5f, bad));
  }
}

void test_apply_curve_linear_and_expo_exact_values() {
  const tandem::AxisCurve linear = tandem::make_linear_curve();
  assert_positive_zero(tandem::apply_curve(-0.0f, linear));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, tandem::apply_curve(0.5f, linear));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, tandem::apply_curve(-0.5f, linear));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::apply_curve(1.5f, linear));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::apply_curve(-1.5f, linear));

  const tandem::AxisCurve expo0 = tandem::make_expo_curve(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, tandem::apply_curve(0.25f, expo0));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, tandem::apply_curve(-0.5f, expo0));

  const tandem::AxisCurve expo1 = tandem::make_expo_curve(1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.125f, tandem::apply_curve(0.5f, expo1));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.125f, tandem::apply_curve(-0.5f, expo1));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.015625f, tandem::apply_curve(0.25f, expo1));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tandem::apply_curve(1.0f, expo1));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, tandem::apply_curve(-1.0f, expo1));

  const tandem::AxisCurve expo_half = tandem::make_expo_curve(0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3125f, tandem::apply_curve(0.5f, expo_half));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.3125f, tandem::apply_curve(-0.5f, expo_half));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1328125f, tandem::apply_curve(0.25f, expo_half));

  const tandem::AxisCurve expo_neg = tandem::make_expo_curve(-0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, tandem::apply_curve(0.5f, expo_neg));

  const tandem::AxisCurve expo_high = tandem::make_expo_curve(2.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.125f, tandem::apply_curve(0.5f, expo_high));
}

void test_apply_curve_monotonicity_symmetry_and_sign_preservation() {
  const tandem::AxisCurve curves[] = {
      tandem::make_linear_curve(),   tandem::make_expo_curve(0.0f),  tandem::make_expo_curve(0.25f),
      tandem::make_expo_curve(0.5f), tandem::make_expo_curve(0.75f), tandem::make_expo_curve(1.0f),
  };

  for (const auto& curve : curves) {
    float prev = -2.0f;
    for (int i = -100; i <= 100; ++i) {
      const float x = static_cast<float>(i) / 100.0f;
      const float y = tandem::apply_curve(x, curve);
      TEST_ASSERT_TRUE(std::isfinite(y) && y >= -1.0f && y <= 1.0f);
      TEST_ASSERT_TRUE(y > prev);
      if (i < 0) {
        TEST_ASSERT_TRUE(y < 0.0f);
      } else if (i > 0) {
        TEST_ASSERT_TRUE(y > 0.0f);
      } else {
        assert_positive_zero(y);
      }
      prev = y;
    }

    for (int i = 1; i <= 100; ++i) {
      const float x = static_cast<float>(i) / 100.0f;
      const float pos = tandem::apply_curve(x, curve);
      const float neg = tandem::apply_curve(-x, curve);
      TEST_ASSERT_TRUE(neg == -pos);
    }
  }
}

void test_apply_curve_guards_non_finite_and_unknown_enum() {
  const float nan_val = std::numeric_limits<float>::quiet_NaN();
  const float inf_val = std::numeric_limits<float>::infinity();

  for (float bad : {nan_val, inf_val, -inf_val}) {
    assert_positive_zero(tandem::apply_curve(bad, tandem::make_linear_curve()));
    assert_positive_zero(tandem::apply_curve(bad, tandem::make_expo_curve(0.5f)));
    assert_positive_zero(tandem::apply_curve(0.5f, tandem::make_expo_curve(bad)));
    assert_positive_zero(tandem::apply_curve(-0.5f, tandem::make_expo_curve(bad)));
  }

  const tandem::AxisCurve unknown{static_cast<tandem::CurveType>(99), 0.5f};
  assert_positive_zero(tandem::apply_curve(0.5f, unknown));
  assert_positive_zero(tandem::apply_curve(-0.5f, unknown));
}

void test_normalized_to_axis_symmetry_rounding_and_guards() {
  TEST_ASSERT_EQUAL_INT16(0, tandem::normalized_to_axis(0.0f));
  TEST_ASSERT_EQUAL_INT16(0, tandem::normalized_to_axis(-0.0f));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, tandem::normalized_to_axis(1.0f));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, tandem::normalized_to_axis(-1.0f));
  TEST_ASSERT_NOT_EQUAL(INT16_MIN, tandem::normalized_to_axis(-1.0f));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, tandem::normalized_to_axis(2.0f));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, tandem::normalized_to_axis(-2.0f));
  TEST_ASSERT_EQUAL_INT16(16384, tandem::normalized_to_axis(0.5f));
  TEST_ASSERT_EQUAL_INT16(-16384, tandem::normalized_to_axis(-0.5f));

  for (int i = 1; i <= 1000; ++i) {
    const float x = static_cast<float>(i) / 1000.0f;
    const int16_t pos = tandem::normalized_to_axis(x);
    const int16_t neg = tandem::normalized_to_axis(-x);
    TEST_ASSERT_EQUAL_INT16(-pos, neg);
    TEST_ASSERT_TRUE(neg >= tandem::kAxisMin && neg != INT16_MIN);
    TEST_ASSERT_TRUE(pos <= tandem::kAxisMax);
  }

  const float nan_val = std::numeric_limits<float>::quiet_NaN();
  const float inf_val = std::numeric_limits<float>::infinity();
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::normalized_to_axis(nan_val));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::normalized_to_axis(inf_val));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::normalized_to_axis(-inf_val));
}

void test_shape_axis_enabled_invert_and_composition() {
  tandem::AxisConfig disabled_cfg{};
  disabled_cfg.calibration = {0, 1000, 2000};
  disabled_cfg.enabled = false;
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::shape_axis(2000, disabled_cfg));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::shape_axis(0, disabled_cfg));

  tandem::AxisConfig linear_cfg{};
  linear_cfg.calibration = {0, 1000, 2000};
  linear_cfg.enabled = true;
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, tandem::shape_axis(0, linear_cfg));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::shape_axis(1000, linear_cfg));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, tandem::shape_axis(2000, linear_cfg));
  TEST_ASSERT_EQUAL_INT16(16384, tandem::shape_axis(1500, linear_cfg));
  TEST_ASSERT_EQUAL_INT16(-16384, tandem::shape_axis(500, linear_cfg));

  linear_cfg.invert = true;
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMax, tandem::shape_axis(0, linear_cfg));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisMin, tandem::shape_axis(2000, linear_cfg));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::shape_axis(1000, linear_cfg));

  tandem::AxisConfig asym_invert{};
  asym_invert.calibration = {100, 1100, 4100};
  asym_invert.invert = true;
  asym_invert.enabled = true;
  TEST_ASSERT_EQUAL_INT16(16384, tandem::shape_axis(600, asym_invert));
  TEST_ASSERT_EQUAL_INT16(-16384, tandem::shape_axis(2600, asym_invert));

  tandem::AxisConfig composed{};
  composed.calibration = {0, 1000, 2000};
  composed.deadzone = 0.2f;
  composed.curve = tandem::make_expo_curve(0.5f);
  composed.invert = false;
  composed.enabled = true;
  TEST_ASSERT_EQUAL_INT16(10240, tandem::shape_axis(1600, composed));
  TEST_ASSERT_EQUAL_INT16(-10240, tandem::shape_axis(400, composed));
  TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, tandem::shape_axis(1150, composed));

  composed.invert = true;
  TEST_ASSERT_EQUAL_INT16(-10240, tandem::shape_axis(1600, composed));
  TEST_ASSERT_EQUAL_INT16(10240, tandem::shape_axis(400, composed));
}

void test_shape_axis_full_adc_sweep_and_center_step() {
  tandem::AxisConfig default_lin{};
  default_lin.enabled = true;
  TEST_ASSERT_EQUAL_INT16(16, tandem::shape_axis(2049, default_lin));
  TEST_ASSERT_EQUAL_INT16(-16, tandem::shape_axis(2047, default_lin));

  tandem::AxisConfig asym_expo{};
  asym_expo.calibration = {100, 1800, 3900};
  asym_expo.deadzone = 0.1f;
  asym_expo.curve = tandem::make_expo_curve(0.6f);
  asym_expo.enabled = true;

  tandem::AxisConfig asym_expo_inv = asym_expo;
  asym_expo_inv.invert = true;

  tandem::AxisConfig sym_cubic_inv{};
  sym_cubic_inv.calibration = {500, 2000, 3500};
  sym_cubic_inv.deadzone = 0.15f;
  sym_cubic_inv.curve = tandem::make_expo_curve(1.0f);
  sym_cubic_inv.invert = true;
  sym_cubic_inv.enabled = true;

  const tandem::AxisConfig configs[] = {
      default_lin,
      asym_expo,
      asym_expo_inv,
      sym_cubic_inv,
  };

  for (const auto& cfg : configs) {
    int16_t prev = 0;
    for (int32_t raw = -10; raw <= 4105; ++raw) {
      const int16_t y = tandem::shape_axis(raw, cfg);
      TEST_ASSERT_TRUE(y >= tandem::kAxisMin && y <= tandem::kAxisMax);
      TEST_ASSERT_NOT_EQUAL(INT16_MIN, y);

      if (raw > -10) {
        if (cfg.invert) {
          TEST_ASSERT_TRUE(y <= prev);
        } else {
          TEST_ASSERT_TRUE(y >= prev);
        }
      }
      if (raw == cfg.calibration.center) {
        TEST_ASSERT_EQUAL_INT16(tandem::kAxisCenter, y);
      }
      if (raw <= cfg.calibration.min) {
        TEST_ASSERT_EQUAL_INT16(cfg.invert ? tandem::kAxisMax : tandem::kAxisMin, y);
      }
      if (raw >= cfg.calibration.max) {
        TEST_ASSERT_EQUAL_INT16(cfg.invert ? tandem::kAxisMin : tandem::kAxisMax, y);
      }
      prev = y;
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_validity_and_factories);
  RUN_TEST(test_calibrate_axis_symmetric);
  RUN_TEST(test_calibrate_axis_asymmetric);
  RUN_TEST(test_calibrate_axis_degenerate_bounds);
  RUN_TEST(test_calibrate_axis_int32_extremes);
  RUN_TEST(test_apply_deadzone_nominal_continuity_and_odd_symmetry);
  RUN_TEST(test_apply_deadzone_guards_and_non_finite);
  RUN_TEST(test_apply_curve_linear_and_expo_exact_values);
  RUN_TEST(test_apply_curve_monotonicity_symmetry_and_sign_preservation);
  RUN_TEST(test_apply_curve_guards_non_finite_and_unknown_enum);
  RUN_TEST(test_normalized_to_axis_symmetry_rounding_and_guards);
  RUN_TEST(test_shape_axis_enabled_invert_and_composition);
  RUN_TEST(test_shape_axis_full_adc_sweep_and_center_step);
  return UNITY_END();
}

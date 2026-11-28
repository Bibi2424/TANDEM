// SPDX-License-Identifier: GPL-3.0-only
#include "tandem/axis_shaping.h"

#include <cmath>

namespace tandem {
namespace {

constexpr float clamp_unit(float value) noexcept {
  if (value <= -1.0f) {
    return -1.0f;
  }
  if (value >= 1.0f) {
    return 1.0f;
  }
  return value;
}

}  // namespace

float calibrate_axis(int32_t raw, const AxisCalibration& calibration) noexcept {
  if (!calibration.is_valid()) {
    return 0.0f;
  }
  if (raw == calibration.center) {
    return 0.0f;
  }
  if (raw <= calibration.min) {
    return -1.0f;
  }
  if (raw >= calibration.max) {
    return 1.0f;
  }

  // Use 64-bit spans so extreme int32_t bounds cannot overflow signed 32-bit subtraction.
  const int64_t delta = static_cast<int64_t>(raw) - static_cast<int64_t>(calibration.center);
  if (delta > 0) {
    const int64_t pos_span = static_cast<int64_t>(calibration.max) - static_cast<int64_t>(calibration.center);
    return clamp_unit(static_cast<float>(delta) / static_cast<float>(pos_span));
  }

  const int64_t neg_span = static_cast<int64_t>(calibration.center) - static_cast<int64_t>(calibration.min);
  const float magnitude = clamp_unit(static_cast<float>(-delta) / static_cast<float>(neg_span));
  return (magnitude <= 0.0f) ? 0.0f : -magnitude;
}

float apply_deadzone(float normalized, float deadzone) noexcept {
  // Non-finite guards throughout this file assume -ffast-math is not enabled.
  if (!std::isfinite(normalized) || !std::isfinite(deadzone)) {
    return 0.0f;
  }
  if (deadzone >= 1.0f) {
    return 0.0f;
  }

  const float x = clamp_unit(normalized);
  if (x == 0.0f) {
    return 0.0f;
  }
  if (deadzone <= 0.0f) {
    return x;
  }

  const float mag = std::fabs(x);
  if (mag <= deadzone) {
    return 0.0f;
  }

  const float scaled = clamp_unit((mag - deadzone) / (1.0f - deadzone));
  if (scaled <= 0.0f) {
    return 0.0f;
  }
  return (x < 0.0f) ? -scaled : scaled;
}

float apply_curve(float normalized, const AxisCurve& curve) noexcept {
  if (!std::isfinite(normalized)) {
    return 0.0f;
  }

  const float x = clamp_unit(normalized);
  switch (curve.type) {
    case CurveType::Linear:
      return (x == 0.0f) ? 0.0f : x;

    case CurveType::Expo: {
      if (!std::isfinite(curve.param)) {
        return 0.0f;
      }
      float e = curve.param;
      if (e < 0.0f) {
        e = 0.0f;
      } else if (e > 1.0f) {
        e = 1.0f;
      }

      if (x == 0.0f) {
        return 0.0f;
      }

      const float mag = std::fabs(x);
      if (mag >= 1.0f) {
        return (x < 0.0f) ? -1.0f : 1.0f;
      }

      const float y_mag = clamp_unit((1.0f - e) * mag + e * mag * mag * mag);
      if (y_mag <= 0.0f) {
        return 0.0f;
      }
      return (x < 0.0f) ? -y_mag : y_mag;
    }

    default:
      // Fail closed at runtime; #6 config/CRC validation must still reject unknown
      // curve IDs so corrupt settings cannot masquerade as a dead axis.
      return 0.0f;
  }
}

int16_t normalized_to_axis(float normalized) noexcept {
  if (!std::isfinite(normalized)) {
    return kAxisCenter;
  }

  const float clamped = clamp_unit(normalized);
  const float scaled = clamped * static_cast<float>(kAxisMax);
  const long rounded = std::lround(scaled);

  if (rounded <= static_cast<long>(kAxisMin)) {
    return kAxisMin;
  }
  if (rounded >= static_cast<long>(kAxisMax)) {
    return kAxisMax;
  }
  return static_cast<int16_t>(rounded);
}

int16_t shape_axis(int32_t raw, const AxisConfig& config) noexcept {
  if (!config.enabled) {
    return kAxisCenter;
  }

  float value = calibrate_axis(raw, config.calibration);
  if (config.invert && value != 0.0f) {
    value = -value;
  }
  value = apply_deadzone(value, config.deadzone);
  value = apply_curve(value, config.curve);
  return normalized_to_axis(value);
}

}  // namespace tandem

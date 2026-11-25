#pragma once

#include <cstdint>
#include <type_traits>

#include "tandem/types.h"

namespace tandem {

// Default 12-bit SAR ADC calibration bounds.
constexpr int32_t kDefaultAxisMin = 0;
constexpr int32_t kDefaultAxisCenter = 2048;
constexpr int32_t kDefaultAxisMax = 4095;

// Per-axis raw ADC calibration endpoints.
struct AxisCalibration {
  int32_t min{kDefaultAxisMin};
  int32_t center{kDefaultAxisCenter};
  int32_t max{kDefaultAxisMax};

  /// Returns true when calibration endpoints are strictly increasing.
  constexpr bool is_valid() const noexcept { return (min < center) && (center < max); }
};

// Fast response curve family (explicit numeric IDs for serialization compatibility).
enum class CurveType : uint8_t {
  Linear = 0,
  Expo = 1,
};

// Response curve descriptor for a single axis.
struct AxisCurve {
  CurveType type{CurveType::Linear};
  float param{0.0f};
};

/// Constructs a linear 1:1 response curve.
constexpr AxisCurve make_linear_curve() noexcept {
  return AxisCurve{CurveType::Linear, 0.0f};
}

/// Constructs a cubic expo response curve with blend factor `expo` in [0, 1].
constexpr AxisCurve make_expo_curve(float expo) noexcept {
  return AxisCurve{CurveType::Expo, expo};
}

// Complete per-axis input shaping configuration.
// `enabled` defaults to false so unused HID axis slots output kAxisCenter.
struct AxisConfig {
  AxisCalibration calibration{};
  float deadzone{0.0f};
  AxisCurve curve{make_linear_curve()};
  bool invert{false};
  bool enabled{false};
};

static_assert(std::is_trivially_copyable_v<AxisCalibration>, "AxisCalibration must be trivially copyable");
static_assert(std::is_trivially_copyable_v<AxisCurve>, "AxisCurve must be trivially copyable");
static_assert(std::is_trivially_copyable_v<AxisConfig>, "AxisConfig must be trivially copyable");

/// Normalizes a raw ADC sample using `calibration` into [-1.0f, +1.0f].
/// Returns +0.0f when calibration bounds are invalid.
float calibrate_axis(int32_t raw, const AxisCalibration& calibration) noexcept;

/// Applies an inner deadzone in [0.0f, 1.0f) with smooth boundary rescaling.
/// `deadzone <= 0.0f` acts as no deadzone (clamped passthrough normalizing -0.0f to +0.0f).
/// Returns +0.0f inside the deadzone, for `deadzone >= 1.0f`, or if either input is non-finite.
float apply_deadzone(float normalized, float deadzone) noexcept;

/// Applies a sign-preserving response curve (`Linear` or cubic `Expo`) in [-1.0f, +1.0f].
/// Returns +0.0f on non-finite inputs/parameters or unknown curve types (fail-closed;
/// config/CRC validation in #6 must still reject unknown CurveType IDs explicitly).
float apply_curve(float normalized, const AxisCurve& curve) noexcept;

/// Quantizes a normalized [-1.0f, +1.0f] value symmetrically to [kAxisMin, kAxisMax].
/// Never returns INT16_MIN (-32768); returns kAxisCenter on non-finite input.
int16_t normalized_to_axis(float normalized) noexcept;

/// Runs the full per-axis pipeline: enabled -> calibrate -> invert -> deadzone -> curve -> quantize.
int16_t shape_axis(int32_t raw, const AxisConfig& config) noexcept;

}  // namespace tandem

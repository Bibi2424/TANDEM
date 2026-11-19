#pragma once

#include <cstdint>

namespace tandem {

// Maximum analog axes per stick (matches the 8 standard HID joystick axes:
// X, Y, Z, Rx, Ry, Rz, Slider, Dial).
constexpr uint8_t kMaxAxes = 8;

// Symmetric signed 16-bit axis range [INT16_MIN + 1, INT16_MAX] = [-32767, +32767].
// Excluding INT16_MIN (-32768) avoids signed negation overflow and keeps +/- deflection symmetric.
constexpr int16_t kAxisMin = static_cast<int16_t>(INT16_MIN + 1);
constexpr int16_t kAxisCenter = 0;
constexpr int16_t kAxisMax = INT16_MAX;

static_assert(kAxisMin == -kAxisMax, "Axis bounds must be symmetric around zero");

constexpr uint8_t kHatCentered = 0;

enum class StickId : uint8_t {
  Unknown = 0,
  Left = 1,
  Right = 2,
};

enum StateFlag : uint8_t {
  kStateFlagNone = 0x00,
  kStateFlagConnected = 0x01,
  kStateFlagFailsafe = 0x02,
};

struct StickState {
  int16_t axes[kMaxAxes]{};
  uint32_t buttons{0};
  uint8_t hat{kHatCentered};
  uint8_t flags{kStateFlagNone};
  uint8_t reserved[2]{0, 0};
};

static_assert(sizeof(StickState) == 24, "StickState must have a fixed 24-byte layout");

constexpr StickState make_centered_state(uint8_t flags = kStateFlagNone) noexcept {
  StickState state{};
  state.flags = flags;
  return state;
}

}  // namespace tandem

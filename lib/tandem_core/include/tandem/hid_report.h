#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/types.h"

namespace tandem {

// Canonical HID Joystick Report ID & fixed payload size (excluding Report ID byte).
//
// BREAKING-CHANGE POLICY:
// Changing kHidReportIdJoystick, kHidJoystickReportBytes, or kHidJoystickReportDescriptor
// alters the host OS / game controller binding contract across Mode A (BLE HID),
// Mode B (USB Base Station), and Mode C (Wired USB HID). Treat any layout change as
// a breaking change requiring explicit versioning and documentation.
inline constexpr uint8_t kHidReportIdJoystick = 1;
inline constexpr size_t kHidJoystickReportBytes = 21;
inline constexpr uint8_t kHidHatMax = 8;

// Canonical 16-bit HID Joystick report descriptor matching StickState 1:1:
// - Report ID 1 (excluded from the 21-byte payload buffer)
// - 8 x 16-bit signed LE axes (X, Y, Z, Rx, Ry, Rz, Slider, Dial), [-32767, +32767]
// - 32 x 1-bit buttons (Usage 1..32, LE uint32_t)
// - 1 x 4-bit 8-way POV hat switch (1..8, Null State = 0 centered) + 4-bit constant pad
inline constexpr uint8_t kHidJoystickReportDescriptor[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x04,  // Usage (Joystick)
    0xA1, 0x01,  // Collection (Application)
    0x85, kHidReportIdJoystick,  // Report ID (1)

    // 8 x 16-bit signed axes: [-32767, +32767]
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x30,        // Usage (X)
    0x09, 0x31,        // Usage (Y)
    0x09, 0x32,        // Usage (Z)
    0x09, 0x33,        // Usage (Rx)
    0x09, 0x34,        // Usage (Ry)
    0x09, 0x35,        // Usage (Rz)
    0x09, 0x36,        // Usage (Slider)
    0x09, 0x37,        // Usage (Dial)
    0x16, 0x01, 0x80,  // Logical Minimum (-32767)
    0x26, 0xFF, 0x7F,  // Logical Maximum (+32767)
    0x75, 0x10,        // Report Size (16)
    0x95, 0x08,        // Report Count (8)
    0x81, 0x02,        // Input (Data, Var, Abs)

    // 32 buttons
    0x05, 0x09,  // Usage Page (Button)
    0x19, 0x01,  // Usage Minimum (Button 1)
    0x29, 0x20,  // Usage Maximum (Button 32)
    0x15, 0x00,  // Logical Minimum (0)
    0x25, 0x01,  // Logical Maximum (1)
    0x75, 0x01,  // Report Size (1)
    0x95, 0x20,  // Report Count (32)
    0x81, 0x02,  // Input (Data, Var, Abs)

    // 8-way POV hat switch (1..8, Null = 0) + 4-bit padding
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x39,        // Usage (Hat Switch)
    0x15, 0x01,        // Logical Minimum (1)
    0x25, 0x08,        // Logical Maximum (8)
    0x35, 0x00,        // Physical Minimum (0)
    0x46, 0x3B, 0x01,  // Physical Maximum (315)
    0x65, 0x14,        // Unit (English Rotation: Angular Position / Degrees)
    0x75, 0x04,        // Report Size (4)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x42,        // Input (Data, Var, Abs, Null State)
    0x75, 0x04,        // Report Size (4)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x03,        // Input (Const, Var, Abs)

    0xC0,  // End Collection
};

inline constexpr size_t kHidJoystickReportDescriptorBytes =
    sizeof(kHidJoystickReportDescriptor);

struct HidJoystickReport {
  uint8_t bytes[kHidJoystickReportBytes]{};
};

static_assert(sizeof(HidJoystickReport) == kHidJoystickReportBytes,
              "HidJoystickReport must be exactly 21 bytes");

/// Encodes `state` into the canonical 21-byte little-endian HID Joystick payload.
/// Clamps axes to [kAxisMin, kAxisMax] (blocking INT16_MIN) and hat > 8 to kHatCentered.
/// Returns false if `out` is null or `out_len < kHidJoystickReportBytes`.
bool encode_hid_joystick_report(const StickState& state,
                                uint8_t* out,
                                size_t out_len) noexcept;

/// Convenience overload returning a packed HidJoystickReport by value.
HidJoystickReport encode_hid_joystick_report(const StickState& state) noexcept;

}  // namespace tandem

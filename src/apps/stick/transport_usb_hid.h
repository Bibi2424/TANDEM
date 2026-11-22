#pragma once

#include <cstddef>
#include <cstdint>

#include "boards/board_config.h"
#include "stick_transport.h"

#if TANDEM_CAP_NATIVE_USB

#include <USBHID.h>

namespace tandem::stick {

class TandemUsbJoystick final : public USBHIDDevice {
 public:
  TandemUsbJoystick();

  void begin();
  bool ready() const;
  bool send(const tandem::StickState& state, uint32_t timeout_ms = 1);
  uint16_t _onGetDescriptor(uint8_t* buffer) override;

 private:
  mutable USBHID hid_{};
};

class UsbHidTransport final : public StickTransport {
 public:
  static void configure_usb_identity() noexcept;
  void init_usb_stack() noexcept;

  uint32_t interval_us() const noexcept override { return 1000; }  // 1000 Hz USB HID
  bool is_active() const noexcept override { return active_; }
  bool start() noexcept override;
  void stop() noexcept override;
  bool send(tandem::StickId stick_id,
            const tandem::StickState& state) noexcept override;
  void format_stats(char* out, size_t out_len) const noexcept override;

 private:
  TandemUsbJoystick joystick_{};
  bool stack_initialized_{false};
  bool active_{false};
  uint32_t sent_{0};
  uint32_t dropped_{0};
};

}  // namespace tandem::stick

#endif  // TANDEM_CAP_NATIVE_USB

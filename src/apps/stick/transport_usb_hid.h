#pragma once

#include <cstddef>
#include <cstdint>

#include "boards/board_config.h"
#include "stick_transport.h"

#if TANDEM_CAP_NATIVE_USB

#include <USBHID.h>
#include "tandem/hid_config_transport.h"
#include "tandem_hal/hid_config_endpoint.h"

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

class TandemUsbConfigHid final : public USBHIDDevice {
 public:
  TandemUsbConfigHid();

  void begin();
  bool ready() const;

  template <typename Handler>
  void drain_commands(Handler&& on_command) noexcept {
    endpoint_.drain_commands(static_cast<Handler&&>(on_command));
  }

  bool enqueue_response(uint8_t seq, const char* data, size_t len) noexcept;
  bool enqueue_live(const char* data, size_t len) noexcept;
  bool poll_tx(uint32_t now_ms, bool& stalled_out) noexcept;
  void reset_tx() noexcept;

  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  void _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;
  void _onSetFeature(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;

 private:
  void handle_rx_chunk(uint8_t report_id, const uint8_t* buffer, uint16_t len) noexcept;

  mutable USBHID hid_{};
  tandem::hal::HidConfigEndpoint endpoint_{
      tandem::config::kHidConfigUsbReportBytes};
};

class UsbHidTransport final : public StickTransport {
 public:
  static void configure_usb_identity() noexcept;
  void init_usb_stack() noexcept;

  uint32_t interval_us() const noexcept override { return 1000; }  // 1000 Hz USB HID
  bool is_active() const noexcept override { return active_; }
  bool start() noexcept override;
  void stop() noexcept override;
  void fill_telemetry(tandem::config::TelemetrySnapshot& out) const noexcept override;
  bool send(tandem::StickId stick_id,
            const tandem::StickState& state) noexcept override;
  void format_stats(char* out, size_t out_len) const noexcept override;

  template <typename Handler>
  void drain_config_commands(Handler&& on_command) noexcept {
    config_hid_.drain_commands(static_cast<Handler&&>(on_command));
  }

  bool send_config_response(uint8_t seq, const char* data, size_t len) noexcept;
  bool send_config_live(const char* data, size_t len) noexcept;
  bool poll_config_tx(uint32_t now_ms, bool& stalled_out) noexcept;

 private:
  // Declaration order ensures Joystick (TLC 1) registers before Vendor Config (TLC 2).
  TandemUsbJoystick joystick_{};
  TandemUsbConfigHid config_hid_{};
  bool stack_initialized_{false};
  bool active_{false};
  uint32_t sent_{0};
  uint32_t dropped_{0};
};

}  // namespace tandem::stick

#endif  // TANDEM_CAP_NATIVE_USB

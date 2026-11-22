#include "transport_usb_hid.h"

#if TANDEM_CAP_NATIVE_USB

#include <USB.h>
#include <esp_mac.h>
#include <cstdio>
#include <cstring>

#include "tandem/device_name.h"
#include "tandem/hid_report.h"

namespace tandem::stick {

TandemUsbJoystick::TandemUsbJoystick() {
  static bool registered = false;
  if (!registered) {
    registered = true;
    USBHID::addDevice(
        this, static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes));
  }
}

void TandemUsbJoystick::begin() { hid_.begin(); }

bool TandemUsbJoystick::ready() const { return hid_.ready(); }

bool TandemUsbJoystick::send(const tandem::StickState& state, uint32_t timeout_ms) {
  if (!hid_.ready()) {
    return false;
  }
  const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
  return hid_.SendReport(
      tandem::kHidReportIdJoystick, report.bytes, sizeof(report.bytes), timeout_ms);
}

uint16_t TandemUsbJoystick::_onGetDescriptor(uint8_t* buffer) {
  std::memcpy(buffer,
              tandem::kHidJoystickReportDescriptor,
              tandem::kHidJoystickReportDescriptorBytes);
  return static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes);
}

void UsbHidTransport::configure_usb_identity() noexcept {
  static bool configured = false;
  if (configured) {
    return;
  }
  configured = true;

  static char product_name[32]{};
  uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    std::memset(mac, 0, sizeof(mac));
  }
  tandem::format_device_name(product_name, sizeof(product_name), "TANDEM-Stick", mac);
  USB.productName(product_name);
  USB.manufacturerName("T.A.N.D.E.M.");
}

void UsbHidTransport::init_usb_stack() noexcept {
  if (!stack_initialized_) {
    configure_usb_identity();
    joystick_.begin();
    USB.begin();
    stack_initialized_ = true;
  }
}

bool UsbHidTransport::start() noexcept {
  init_usb_stack();
  active_ = true;
  return true;
}

void UsbHidTransport::stop() noexcept {
  if (active_) {
    // Release all buttons/axes on the host before pausing HID reports so USB CDC stays alive
    // without leaving stale held inputs. Wait briefly for any in-flight 1 kHz report to clear.
    const tandem::StickState centered = tandem::make_centered_state(0);
    const uint32_t start_ms = millis();
    while (!joystick_.send(centered, 20) &&
           static_cast<uint32_t>(millis() - start_ms) < 50U) {
      delay(1);
    }
  }
  active_ = false;
}

bool UsbHidTransport::send(tandem::StickId,
                           const tandem::StickState& state) noexcept {
  if (!active_) {
    return false;
  }
  if (joystick_.send(state)) {
    ++sent_;
    return true;
  }
  ++dropped_;
  return false;
}

void UsbHidTransport::format_stats(char* out, size_t out_len) const noexcept {
  std::snprintf(out,
                out_len,
                "ready=%d sent=%lu drop=%lu",
                joystick_.ready() ? 1 : 0,
                static_cast<unsigned long>(sent_),
                static_cast<unsigned long>(dropped_));
}

}  // namespace tandem::stick

#endif  // TANDEM_CAP_NATIVE_USB

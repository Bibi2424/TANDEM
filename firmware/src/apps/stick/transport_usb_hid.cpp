// SPDX-License-Identifier: GPL-3.0-only
#include "transport_usb_hid.h"

#if TANDEM_CAP_NATIVE_USB

#include <USB.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>

#include "tandem/device_name.h"
#include "tandem/hid_report.h"

namespace tandem::stick {

namespace {

bool send_usb_config_chunk(void* ctx, const uint8_t* data, size_t len) noexcept {
  auto* hid = static_cast<USBHID*>(ctx);
  return hid != nullptr && hid->ready() && hid->SendReport(tandem::config::kHidReportIdConfigUsb, data, len, 5U);
}

}  // namespace

TandemUsbJoystick::TandemUsbJoystick() {
  static bool registered = false;
  if (!registered) {
    registered = true;
    USBHID::addDevice(this, static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes));
  }
}

void TandemUsbJoystick::begin() {
  hid_.begin();
}

bool TandemUsbJoystick::ready() const {
  return hid_.ready();
}

bool TandemUsbJoystick::send(const tandem::StickState& state, uint32_t timeout_ms) {
  if (!hid_.ready()) {
    return false;
  }
  const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
  return hid_.SendReport(tandem::kHidReportIdJoystick, report.bytes, sizeof(report.bytes), timeout_ms);
}

uint16_t TandemUsbJoystick::_onGetDescriptor(uint8_t* buffer) {
  std::memcpy(buffer, tandem::kHidJoystickReportDescriptor, tandem::kHidJoystickReportDescriptorBytes);
  return static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes);
}

TandemUsbConfigHid::TandemUsbConfigHid() {
  static bool registered = false;
  if (!registered) {
    registered = true;
    USBHID::addDevice(this, static_cast<uint16_t>(tandem::config::kHidConfigReportDescriptorBytes));
  }
}

void TandemUsbConfigHid::begin() {
  hid_.begin();
}

bool TandemUsbConfigHid::ready() const {
  return hid_.ready();
}

uint16_t TandemUsbConfigHid::_onGetDescriptor(uint8_t* buffer) {
  std::memcpy(buffer, tandem::config::kHidConfigUsbReportDescriptor.bytes, tandem::config::kHidConfigReportDescriptorBytes);
  return static_cast<uint16_t>(tandem::config::kHidConfigReportDescriptorBytes);
}

void TandemUsbConfigHid::handle_rx_chunk(uint8_t report_id, const uint8_t* buffer, uint16_t len) noexcept {
  if (report_id != tandem::config::kHidReportIdConfigUsb) {
    return;
  }
  endpoint_.on_rx_chunk(buffer, len);
}

void TandemUsbConfigHid::_onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
  handle_rx_chunk(report_id, buffer, len);
}

void TandemUsbConfigHid::_onSetFeature(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
  // Arduino-ESP32 USBHID routes Control SET_REPORT with non-zero report_id to _onSetFeature.
  handle_rx_chunk(report_id, buffer, len);
}

bool TandemUsbConfigHid::enqueue_response(uint8_t seq, const char* data, size_t len) noexcept {
  return endpoint_.enqueue_response(seq, data, len);
}

bool TandemUsbConfigHid::enqueue_live(const char* data, size_t len) noexcept {
  return endpoint_.enqueue_live(data, len);
}

bool TandemUsbConfigHid::poll_tx(uint32_t now_ms, bool& stalled_out) noexcept {
  return endpoint_.poll_tx(now_ms, send_usb_config_chunk, &hid_, stalled_out);
}

void TandemUsbConfigHid::reset_tx() noexcept {
  endpoint_.reset_tx();
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
    config_hid_.begin();
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
    while (!joystick_.send(centered, 20) && static_cast<uint32_t>(millis() - start_ms) < 50U) {
      delay(1);
    }
  }
  active_ = false;
}

bool UsbHidTransport::send(tandem::StickId, const tandem::StickState& state) noexcept {
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

void UsbHidTransport::fill_telemetry(tandem::config::TelemetrySnapshot& out) const noexcept {
  out.has_dropped = true;
  out.dropped_packets = dropped_;
  out.has_peer_status = true;
  out.peer_status = joystick_.ready() ? "USB_READY" : "USB_WAIT";
}

void UsbHidTransport::format_stats(char* out, size_t out_len) const noexcept {
  std::snprintf(out, out_len, "ready=%d sent=%lu drop=%lu", joystick_.ready() ? 1 : 0, static_cast<unsigned long>(sent_),
                static_cast<unsigned long>(dropped_));
}

bool UsbHidTransport::send_config_response(uint8_t seq, const char* data, size_t len) noexcept {
  return config_hid_.enqueue_response(seq, data, len);
}

bool UsbHidTransport::send_config_live(const char* data, size_t len) noexcept {
  return config_hid_.enqueue_live(data, len);
}

bool UsbHidTransport::poll_config_tx(uint32_t now_ms, bool& stalled_out) noexcept {
  return config_hid_.poll_tx(now_ms, stalled_out);
}

}  // namespace tandem::stick

#endif  // TANDEM_CAP_NATIVE_USB

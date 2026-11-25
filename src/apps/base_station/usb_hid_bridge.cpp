#include "usb_hid_bridge.h"

#include <Arduino.h>
#include <USB.h>
#include <esp_mac.h>

#include <cstring>

#include "esp32-hal-tinyusb.h"
#include "tandem/device_name.h"

namespace tandem::base_usb {

namespace {

static_assert(tandem::kHidJoystickReportDescriptor[6] == 0x85U &&
                  tandem::kHidJoystickReportDescriptor[7] ==
                      tandem::kHidReportIdJoystick,
              "Expected Report ID byte at offset 7 in canonical HID descriptor");

uint8_t g_report_descriptor[kReportDescriptorBytes]{};
tandem::hal::HidConfigEndpoint g_webhid_endpoint{
    tandem::config::kHidConfigUsbReportBytes};

void init_report_descriptor() noexcept {
  std::memcpy(g_report_descriptor,
              tandem::kHidJoystickReportDescriptor,
              tandem::kHidJoystickReportDescriptorBytes);
  g_report_descriptor[7] = kReportIdLeft;
  if constexpr (!kCombinedMode) {
    std::memcpy(&g_report_descriptor[tandem::kHidJoystickReportDescriptorBytes],
                tandem::kHidJoystickReportDescriptor,
                tandem::kHidJoystickReportDescriptorBytes);
    g_report_descriptor[tandem::kHidJoystickReportDescriptorBytes + 7] =
        kReportIdRight;
  }
  std::memcpy(&g_report_descriptor[kJoystickDescriptorBytes],
              tandem::config::kHidConfigUsbReportDescriptor.bytes,
              tandem::config::kHidConfigReportDescriptorBytes);
}

bool send_usb_config_chunk(void*, const uint8_t* data, size_t len) noexcept {
  return tud_hid_n_ready(0) &&
         tud_hid_n_report(0,
                          tandem::config::kHidReportIdConfigUsb,
                          data,
                          static_cast<uint16_t>(len));
}

}  // namespace

}  // namespace tandem::base_usb

extern "C" uint16_t tandem_hid_load_descriptor(uint8_t* dst, uint8_t* itf) {
  static bool initialized = false;
  if (initialized) {
    return 0;
  }
  initialized = true;

  const uint8_t str_index =
      tinyusb_add_string_descriptor(tandem::base_usb::kHidInterfaceName);
  const uint8_t ep_in = tinyusb_get_free_in_endpoint();
  const uint8_t ep_out = tinyusb_get_free_out_endpoint();
  if (ep_in == 0 || ep_out == 0) {
    return 0;
  }

  const uint8_t descriptor[TUD_HID_INOUT_DESC_LEN] = {
      TUD_HID_INOUT_DESCRIPTOR(*itf,
                               str_index,
                               HID_ITF_PROTOCOL_NONE,
                               static_cast<uint16_t>(
                                   tandem::base_usb::kReportDescriptorBytes),
                               ep_out,
                               static_cast<uint8_t>(0x80U | ep_in),
                               CFG_TUD_ENDPOINT_SIZE,
                               1)};
  *itf += 1;
  std::memcpy(dst, descriptor, TUD_HID_INOUT_DESC_LEN);
  return TUD_HID_INOUT_DESC_LEN;
}

extern "C" uint8_t const* tud_hid_descriptor_report_cb(uint8_t) {
  return tandem::base_usb::g_report_descriptor;
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t,
                                          uint8_t,
                                          hid_report_type_t,
                                          uint8_t*,
                                          uint16_t) {
  return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t,
                                      uint8_t report_id,
                                      hid_report_type_t report_type,
                                      uint8_t const* buffer,
                                      uint16_t bufsize) {
  if (buffer == nullptr || bufsize == 0U) {
    return;
  }
  if (report_id == 0U &&
      (report_type == 0 || report_type == HID_REPORT_TYPE_OUTPUT)) {
    if (buffer[0] == tandem::config::kHidReportIdConfigUsb && bufsize > 1U) {
      tandem::base_usb::g_webhid_endpoint.on_rx_chunk(
          buffer + 1, static_cast<size_t>(bufsize - 1U));
    }
    return;
  }
  if (report_id == tandem::config::kHidReportIdConfigUsb &&
      (report_type == HID_REPORT_TYPE_OUTPUT ||
       report_type == HID_REPORT_TYPE_FEATURE)) {
    tandem::base_usb::g_webhid_endpoint.on_rx_chunk(buffer, bufsize);
  }
}

namespace tandem::base_usb {

namespace {

// Must register the TinyUSB HID interface before initArduino() starts USB CDC
// when ARDUINO_USB_CDC_ON_BOOT=1.
struct TandemHidRegistrar {
  TandemHidRegistrar() {
    init_report_descriptor();
    tinyusb_enable_interface2(
        USB_INTERFACE_HID, TUD_HID_INOUT_DESC_LEN, tandem_hid_load_descriptor, false);
  }
};

TandemHidRegistrar g_hid_registrar;

}  // namespace

void begin() noexcept {
  static char product_name[32]{};
  uint8_t base_mac[6]{};
  if (esp_efuse_mac_get_default(base_mac) != ESP_OK) {
    std::memset(base_mac, 0, sizeof(base_mac));
  }
  tandem::format_device_name(
      product_name, sizeof(product_name), "TANDEM-Base", base_mac);

  USB.VID(kUsbVid);
  USB.PID(kUsbPid);
  USB.manufacturerName("T.A.N.D.E.M.");
  USB.productName(product_name);
  USB.begin();
}

bool send_joystick(uint8_t report_id, const tandem::StickState& state) noexcept {
  if (!tud_hid_n_ready(0)) {
    return false;
  }
  const tandem::HidJoystickReport report =
      tandem::encode_hid_joystick_report(state);
  return tud_hid_n_report(
      0, report_id, report.bytes, static_cast<uint16_t>(sizeof(report.bytes)));
}

tandem::hal::HidConfigEndpoint& config_endpoint() noexcept {
  return g_webhid_endpoint;
}

bool send_config_response(uint8_t seq, const char* data, size_t len) noexcept {
  return g_webhid_endpoint.enqueue_response(seq, data, len);
}

bool send_config_live(const char* data, size_t len) noexcept {
  return g_webhid_endpoint.enqueue_live(data, len);
}

bool poll_config_tx(uint32_t now_ms, bool& stalled_out) noexcept {
  return g_webhid_endpoint.poll_tx(
      now_ms, send_usb_config_chunk, nullptr, stalled_out);
}

}  // namespace tandem::base_usb

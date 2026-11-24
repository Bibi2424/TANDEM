#include <Arduino.h>
#include <USB.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>

#if !ARDUINO_USB_CDC_ON_BOOT
#include <USBCDC.h>
namespace {
USBCDC g_usb_serial;
}  // namespace
#ifdef Serial
#undef Serial
#endif
#define Serial g_usb_serial
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "boards/board_config.h"
#include "esp32-hal-tinyusb.h"
#include "tandem/config_protocol.h"
#include "tandem/core_info.h"
#include "tandem/device_name.h"
#include "tandem/espnow_packet.h"
#include "tandem/failsafe.h"
#include "tandem/hid_report.h"
#include "tandem/stick_combiner.h"
#include "tandem/types.h"

static_assert(tandem::board::kHasNativeUsb, "base_station requires native USB support");
static_assert(tandem::board::kHasEspNow, "base_station requires ESP-NOW support");

static_assert(tandem::kHidJoystickReportDescriptor[6] == 0x85U &&
                  tandem::kHidJoystickReportDescriptor[7] == tandem::kHidReportIdJoystick,
              "Expected Report ID byte at offset 7 in canonical HID descriptor");

#ifndef TANDEM_ESPNOW_CHANNEL
#define TANDEM_ESPNOW_CHANNEL 1
#endif

#ifndef TANDEM_FAILSAFE_TIMEOUT_MS
#define TANDEM_FAILSAFE_TIMEOUT_MS 200
#endif

#ifndef TANDEM_BASE_COMBINED_MODE
#define TANDEM_BASE_COMBINED_MODE 0
#endif

namespace {

constexpr bool kCombinedMode = (TANDEM_BASE_COMBINED_MODE != 0);
constexpr uint8_t kEspNowChannel = static_cast<uint8_t>(TANDEM_ESPNOW_CHANNEL);
constexpr uint32_t kFailsafeTimeoutMs =
    static_cast<uint32_t>(TANDEM_FAILSAFE_TIMEOUT_MS);
constexpr uint32_t kUsbPollIntervalUs = 1000;  // 1000 Hz USB HID target
constexpr uint32_t kStatusIntervalMs = 2000;
constexpr uint32_t kLiveStreamIntervalMs = 33;
constexpr uint32_t kLedConnectedIntervalMs = 250;
constexpr uint32_t kLedIdleIntervalMs = 1000;
constexpr uint32_t kUsbSerialWaitMs = 1500;

constexpr uint16_t kUsbVid = 0x303AU;
constexpr uint16_t kUsbPid = kCombinedMode ? 0x80C5U : 0x80C4U;
constexpr const char* kHidInterfaceName =
    kCombinedMode ? "TANDEM Combined Stick" : "TANDEM Base Station";

constexpr uint8_t kReportIdLeft = 1;
constexpr uint8_t kReportIdRight = 2;
constexpr size_t kReportDescriptorBytes =
    kCombinedMode ? tandem::kHidJoystickReportDescriptorBytes
                  : (tandem::kHidJoystickReportDescriptorBytes * 2U);

uint8_t g_report_descriptor[kReportDescriptorBytes]{};

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
}

bool send_hid_report(uint8_t report_id, const tandem::StickState& state) noexcept {
  if (!tud_hid_n_ready(0)) {
    return false;
  }
  const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
  return tud_hid_n_report(0, report_id, report.bytes, static_cast<uint16_t>(sizeof(report.bytes)));
}

struct RxStickSnapshot {
  tandem::StickState state{tandem::make_centered_state(tandem::kStateFlagFailsafe)};
  uint16_t seq{0};
  int8_t rssi{0};
  bool pending{false};
};

portMUX_TYPE g_rx_mux = portMUX_INITIALIZER_UNLOCKED;
RxStickSnapshot g_rx_snapshots[2]{};
tandem::EspNowLinkStats g_link_stats[2]{};
std::atomic<uint32_t> g_decode_errors{0};

void on_espnow_recv(const esp_now_recv_info_t* esp_now_info,
                    const uint8_t* data,
                    int data_len) {
  if (data == nullptr || data_len <= 0) {
    g_decode_errors.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  tandem::DecodedEspNowPacket decoded{};
  if (tandem::decode_espnow_packet(
          data, static_cast<size_t>(data_len), decoded) !=
      tandem::PacketDecodeResult::Ok) {
    g_decode_errors.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  const size_t idx = (decoded.stick_id == tandem::StickId::Left) ? 0U : 1U;
  const int8_t rssi = (esp_now_info != nullptr && esp_now_info->rx_ctrl != nullptr)
                          ? static_cast<int8_t>(esp_now_info->rx_ctrl->rssi)
                          : 0;

  portENTER_CRITICAL(&g_rx_mux);
  g_rx_snapshots[idx].state = decoded.state;
  g_rx_snapshots[idx].seq = decoded.seq;
  g_rx_snapshots[idx].rssi = rssi;
  g_rx_snapshots[idx].pending = true;
  g_link_stats[idx].record_seq(decoded.seq);
  portEXIT_CRITICAL(&g_rx_mux);
}

tandem::FailsafeMonitor g_monitors[2]{};
tandem::StickCombineConfig g_combine_config = tandem::make_default_combine_config();

uint32_t g_last_usb_us = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_live_ms = 0;
uint32_t g_last_led_ms = 0;
uint32_t g_rate_window_ms = 0;
uint32_t g_rx_pkt_count = 0;
uint32_t g_tx_report_count = 0;
uint16_t g_measured_rx_hz = 0;
uint16_t g_measured_tx_hz = 0;
bool g_led_state = false;
bool g_live_streaming = false;
uint32_t g_usb_sent = 0;
uint32_t g_usb_dropped = 0;
[[maybe_unused]] size_t g_next_dual_idx = 0;
int8_t g_last_rssi[2]{0, 0};
char g_peer_status_buf[32]{"L:DISC R:DISC"};
char g_cmd_buf[tandem::config::kMaxCommandLength]{};
size_t g_cmd_len = 0;
bool g_cmd_reading_line = false;

const char* link_status_str(tandem::LinkStatus status) noexcept {
  switch (status) {
    case tandem::LinkStatus::Connected:
      return "CONN";
    case tandem::LinkStatus::TimedOut:
      return "TOUT";
    case tandem::LinkStatus::Disconnected:
    default:
      return "DISC";
  }
}

tandem::config::TelemetrySnapshot build_base_telemetry() noexcept {
  std::snprintf(g_peer_status_buf,
                sizeof(g_peer_status_buf),
                "L:%s R:%s",
                link_status_str(g_monitors[0].status()),
                link_status_str(g_monitors[1].status()));

  tandem::EspNowLinkStats stats_copy[2]{};
  portENTER_CRITICAL(&g_rx_mux);
  stats_copy[0] = g_link_stats[0];
  stats_copy[1] = g_link_stats[1];
  portEXIT_CRITICAL(&g_rx_mux);

  tandem::config::TelemetrySnapshot tlm{};
  tlm.has_sample_hz = true;
  tlm.sample_hz = g_measured_rx_hz;
  tlm.has_tx_hz = true;
  tlm.tx_hz = g_measured_tx_hz;
  tlm.has_dropped = true;
  tlm.dropped_packets = stats_copy[0].dropped_packets +
                        stats_copy[1].dropped_packets +
                        g_decode_errors.load(std::memory_order_relaxed);

  const bool left_conn = g_monitors[0].is_connected();
  const bool right_conn = g_monitors[1].is_connected();
  tlm.has_failsafe = true;
  tlm.failsafe_active =
      (!left_conn && !right_conn) ||
      (g_monitors[0].status() == tandem::LinkStatus::TimedOut) ||
      (g_monitors[1].status() == tandem::LinkStatus::TimedOut);

  if (left_conn || right_conn) {
    tlm.has_rssi = true;
    tlm.rssi_dbm = left_conn ? g_last_rssi[0] : g_last_rssi[1];
  }
  tlm.has_peer_status = true;
  tlm.peer_status = g_peer_status_buf;
  return tlm;
}

tandem::config::DeviceCapabilities build_base_capabilities() noexcept {
  const tandem::config::TelemetrySnapshot tlm = build_base_telemetry();
  tandem::config::DeviceCapabilities caps{};
  caps.board = tandem::board::kBoardName;
  caps.stick_id = tandem::StickId::Unknown;
  caps.is_base_station = true;
  caps.has_config = false;
  caps.transport_mode = kCombinedMode ? "ESPNOW_RX_COMBINED" : "ESPNOW_RX_DUAL";
  caps.synthetic = false;
  caps.axes_mask = 0xFFU;
  caps.buttons_mask = 0xFFFFFFFFUL;
  caps.has_hat = true;
  caps.adc_max = tandem::kDefaultAxisMax;
  caps.transports_mask = tandem::config::kTransportMaskEspNowRx |
                         tandem::config::kTransportMaskUsbHid |
                         tandem::config::kTransportMaskUsbSerial;
  caps.fw_version = tandem::core_version();
  caps.git_commit = tandem::core_git_commit();
  caps.peer_status = tlm.peer_status;
  return caps;
}

void handle_serial_config_line(const char* line) noexcept {
  tandem::config::ConfigCallbacks cbs{};
  cbs.on_live_changed = [](void*, bool enabled) {
    g_live_streaming = enabled;
    g_last_live_ms = millis();
  };
  cbs.on_get_telemetry = [](void*, tandem::config::TelemetrySnapshot& out) -> bool {
    out = build_base_telemetry();
    return true;
  };
  char resp[tandem::config::kMaxResponseLength]{};
  tandem::StickInputConfig unused_cfg{};
  const size_t n = tandem::config::process_command(
      line, build_base_capabilities(), unused_cfg, cbs, resp, sizeof(resp));
  if (n > 0) {
    Serial.print(resp);
  }
}

bool setup_espnow_rx() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    return false;
  }
  return esp_now_register_recv_cb(on_espnow_recv) == ESP_OK;
}

}  // namespace

extern "C" uint16_t tandem_hid_load_descriptor(uint8_t* dst, uint8_t* itf) {
  static bool initialized = false;
  if (initialized) {
    return 0;
  }
  initialized = true;

  const uint8_t str_index = tinyusb_add_string_descriptor(kHidInterfaceName);
  const uint8_t ep_in = tinyusb_get_free_in_endpoint();
  const uint8_t ep_out = tinyusb_get_free_out_endpoint();
  if (ep_in == 0 || ep_out == 0) {
    return 0;
  }

  const uint8_t descriptor[TUD_HID_INOUT_DESC_LEN] = {
      TUD_HID_INOUT_DESCRIPTOR(*itf,
                               str_index,
                               HID_ITF_PROTOCOL_NONE,
                               static_cast<uint16_t>(kReportDescriptorBytes),
                               ep_out,
                               static_cast<uint8_t>(0x80U | ep_in),
                               CFG_TUD_ENDPOINT_SIZE,
                               1)};
  *itf += 1;
  std::memcpy(dst, descriptor, TUD_HID_INOUT_DESC_LEN);
  return TUD_HID_INOUT_DESC_LEN;
}

extern "C" uint8_t const* tud_hid_descriptor_report_cb(uint8_t) {
  return g_report_descriptor;
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t,
                                          uint8_t,
                                          hid_report_type_t,
                                          uint8_t*,
                                          uint16_t) {
  return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t,
                                      uint8_t,
                                      hid_report_type_t,
                                      uint8_t const*,
                                      uint16_t) {}

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

void setup() {
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    pinMode(tandem::board::kPinStatusLed, OUTPUT);
    digitalWrite(tandem::board::kPinStatusLed, LOW);
  }

  tandem::FailsafeConfig fs_cfg{};
  fs_cfg.timeout_ms = kFailsafeTimeoutMs;
  fs_cfg.failsafe_state = tandem::make_centered_state(tandem::kStateFlagFailsafe);
  g_monitors[0].set_config(fs_cfg);
  g_monitors[1].set_config(fs_cfg);

  static char product_name[32]{};
  uint8_t base_mac[6]{};
  if (esp_efuse_mac_get_default(base_mac) != ESP_OK) {
    std::memset(base_mac, 0, sizeof(base_mac));
  }
  tandem::format_device_name(product_name, sizeof(product_name), "TANDEM-Base", base_mac);

  USB.VID(kUsbVid);
  USB.PID(kUsbPid);
  USB.manufacturerName("T.A.N.D.E.M.");
  USB.productName(product_name);
  USB.begin();

  const bool espnow_ok = setup_espnow_rx();
  uint8_t local_mac[6]{};
  esp_wifi_get_mac(WIFI_IF_STA, local_mac);

  Serial.begin(115200);
  const uint32_t wait_start_ms = millis();
  while (!Serial && static_cast<uint32_t>(millis() - wait_start_ms) < kUsbSerialWaitMs) {
    delay(10);
  }
  delay(100);

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. ESP-NOW USB Base Station (Mode B) ===");
  Serial.printf("Board      : %s\n", tandem::board::kBoardName);
  Serial.printf("Base MAC   : %02X:%02X:%02X:%02X:%02X:%02X (ch=%u)\n",
                local_mac[0], local_mac[1], local_mac[2],
                local_mac[3], local_mac[4], local_mac[5],
                static_cast<unsigned>(kEspNowChannel));
  Serial.printf("ESP-NOW    : %s (failsafe=%lu ms)\n",
                espnow_ok ? "READY" : "INIT FAILED",
                static_cast<unsigned long>(kFailsafeTimeoutMs));
  Serial.printf("USB HID    : %s (VID=0x%04X PID=0x%04X, %s, %u-byte desc)\n",
                kHidInterfaceName,
                static_cast<unsigned>(kUsbVid),
                static_cast<unsigned>(kUsbPid),
                kCombinedMode ? "Combined 1x" : "Dual 2x",
                static_cast<unsigned>(kReportDescriptorBytes));

  const uint32_t now_ms = millis();
  g_last_usb_us = micros();
  g_last_status_ms = now_ms;
  g_last_led_ms = now_ms;
  g_rate_window_ms = now_ms;
}

void poll_serial_config() noexcept {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (g_cmd_reading_line) {
      if (ch == '\r' || ch == '\n') {
        g_cmd_buf[g_cmd_len] = '\0';
        g_cmd_reading_line = false;
        g_cmd_len = 0;
        handle_serial_config_line(g_cmd_buf);
      } else if (g_cmd_len + 1 < sizeof(g_cmd_buf)) {
        g_cmd_buf[g_cmd_len++] = static_cast<char>(ch);
      } else {
        g_cmd_reading_line = false;
        g_cmd_len = 0;
        Serial.printf("%s{\"v\":%u,\"cmd\":\"unknown\",\"ok\":false,\"err\":\"line_too_long\"}\n",
                      tandem::config::kResponsePrefix,
                      static_cast<unsigned>(tandem::config::kProtocolVersion));
      }
      continue;
    }

    if (ch == '$') {
      g_cmd_reading_line = true;
      g_cmd_buf[0] = '$';
      g_cmd_len = 1;
    }
  }
}

void loop() {
  const uint32_t now_ms = millis();
  poll_serial_config();
  const uint32_t now_us = micros();

  if (static_cast<uint32_t>(now_us - g_last_usb_us) >= kUsbPollIntervalUs) {
    g_last_usb_us = now_us;

    RxStickSnapshot local_snap[2]{};
    portENTER_CRITICAL(&g_rx_mux);
    for (size_t i = 0; i < 2U; ++i) {
      if (g_rx_snapshots[i].pending) {
        local_snap[i] = g_rx_snapshots[i];
        g_rx_snapshots[i].pending = false;
      }
    }
    portEXIT_CRITICAL(&g_rx_mux);

    for (size_t i = 0; i < 2U; ++i) {
      if (local_snap[i].pending) {
        const bool was_connected = g_monitors[i].is_connected();
        g_monitors[i].on_packet(local_snap[i].state, now_ms);
        g_last_rssi[i] = local_snap[i].rssi;
        ++g_rx_pkt_count;
        if (!was_connected && g_monitors[i].is_connected()) {
          portENTER_CRITICAL(&g_rx_mux);
          g_link_stats[i].reset();
          g_link_stats[i].record_seq(local_snap[i].seq);
          portEXIT_CRITICAL(&g_rx_mux);
        }
      }
      g_monitors[i].update(now_ms);
    }

    if constexpr (kCombinedMode) {
      const tandem::StickState combined = tandem::combine_sticks(
          g_monitors[0].state(), g_monitors[1].state(), g_combine_config);
      if (send_hid_report(kReportIdLeft, combined)) {
        ++g_usb_sent;
        ++g_tx_report_count;
      } else {
        ++g_usb_dropped;
      }
    } else {
      const uint8_t report_ids[2] = {kReportIdLeft, kReportIdRight};
      if (send_hid_report(report_ids[g_next_dual_idx],
                          g_monitors[g_next_dual_idx].state())) {
        ++g_usb_sent;
        ++g_tx_report_count;
        g_next_dual_idx ^= 1U;
      } else {
        ++g_usb_dropped;
      }
    }
  }

  const uint32_t rate_elapsed_ms = static_cast<uint32_t>(now_ms - g_rate_window_ms);
  if (rate_elapsed_ms >= 1000U) {
    g_measured_rx_hz = static_cast<uint16_t>(
        (g_rx_pkt_count * 1000UL) / rate_elapsed_ms);
    g_measured_tx_hz = static_cast<uint16_t>(
        (g_tx_report_count * 1000UL) / rate_elapsed_ms);
    g_rx_pkt_count = 0;
    g_tx_report_count = 0;
    g_rate_window_ms = now_ms;
  }

  const bool any_connected =
      g_monitors[0].is_connected() || g_monitors[1].is_connected();
  const uint32_t led_interval =
      any_connected ? kLedConnectedIntervalMs : kLedIdleIntervalMs;
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    if (static_cast<uint32_t>(now_ms - g_last_led_ms) >= led_interval) {
      g_last_led_ms = now_ms;
      g_led_state = !g_led_state;
      digitalWrite(tandem::board::kPinStatusLed, g_led_state ? HIGH : LOW);
    }
  }

  if (g_live_streaming) {
    if (static_cast<uint32_t>(now_ms - g_last_live_ms) >= kLiveStreamIntervalMs) {
      g_last_live_ms = now_ms;
      const tandem::RawStickInput raw_zero{};
      const tandem::StickState active_state =
          kCombinedMode
              ? tandem::combine_sticks(
                    g_monitors[0].state(), g_monitors[1].state(), g_combine_config)
              : ((g_monitors[0].is_connected() || !g_monitors[1].is_connected())
                     ? g_monitors[0].state()
                     : g_monitors[1].state());
      char resp[tandem::config::kMaxResponseLength]{};
      const size_t n = tandem::config::format_live_response(
          raw_zero, active_state, false, build_base_telemetry(), resp, sizeof(resp));
      if (n > 0) {
        Serial.print(resp);
      }
    }
  } else if (static_cast<uint32_t>(now_ms - g_last_status_ms) >= kStatusIntervalMs) {
    g_last_status_ms = now_ms;

    tandem::EspNowLinkStats stats_copy[2]{};
    portENTER_CRITICAL(&g_rx_mux);
    stats_copy[0] = g_link_stats[0];
    stats_copy[1] = g_link_stats[1];
    portEXIT_CRITICAL(&g_rx_mux);

    Serial.printf(
        "[%lu ms] L:%s(rx=%lu drop=%lu ooo=%lu rssi=%d x=%d) R:%s(rx=%lu drop=%lu ooo=%lu rssi=%d x=%d) | err=%lu usb=%lu/%lu\n",
        static_cast<unsigned long>(now_ms),
        link_status_str(g_monitors[0].status()),
        static_cast<unsigned long>(stats_copy[0].rx_packets),
        static_cast<unsigned long>(stats_copy[0].dropped_packets),
        static_cast<unsigned long>(stats_copy[0].out_of_order_or_dup),
        static_cast<int>(g_last_rssi[0]),
        static_cast<int>(g_monitors[0].state().axes[0]),
        link_status_str(g_monitors[1].status()),
        static_cast<unsigned long>(stats_copy[1].rx_packets),
        static_cast<unsigned long>(stats_copy[1].dropped_packets),
        static_cast<unsigned long>(stats_copy[1].out_of_order_or_dup),
        static_cast<int>(g_last_rssi[1]),
        static_cast<int>(g_monitors[1].state().axes[0]),
        static_cast<unsigned long>(g_decode_errors.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(g_usb_sent),
        static_cast<unsigned long>(g_usb_dropped));
  }
}

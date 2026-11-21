#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "boards/board_config.h"
#include "tandem/espnow_packet.h"
#include "tandem/failsafe.h"
#include "tandem/hid_report.h"
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

namespace {

constexpr uint8_t kEspNowChannel = static_cast<uint8_t>(TANDEM_ESPNOW_CHANNEL);
constexpr uint32_t kFailsafeTimeoutMs =
    static_cast<uint32_t>(TANDEM_FAILSAFE_TIMEOUT_MS);
constexpr uint32_t kUsbPollIntervalUs = 1000;  // 1000 Hz USB HID target
constexpr uint32_t kStatusIntervalMs = 2000;
constexpr uint32_t kLedConnectedIntervalMs = 250;
constexpr uint32_t kLedIdleIntervalMs = 1000;
constexpr uint32_t kUsbSerialWaitMs = 1500;

constexpr uint8_t kReportIdLeft = 1;
constexpr uint8_t kReportIdRight = 2;
constexpr size_t kDualReportDescriptorBytes =
    tandem::kHidJoystickReportDescriptorBytes * 2U;

uint8_t g_dual_report_descriptor[kDualReportDescriptorBytes]{};

void init_dual_report_descriptor() noexcept {
  std::memcpy(g_dual_report_descriptor,
              tandem::kHidJoystickReportDescriptor,
              tandem::kHidJoystickReportDescriptorBytes);
  std::memcpy(&g_dual_report_descriptor[tandem::kHidJoystickReportDescriptorBytes],
              tandem::kHidJoystickReportDescriptor,
              tandem::kHidJoystickReportDescriptorBytes);
  g_dual_report_descriptor[7] = kReportIdLeft;
  g_dual_report_descriptor[tandem::kHidJoystickReportDescriptorBytes + 7] =
      kReportIdRight;
}

class TandemDualUsbJoystick final : public USBHIDDevice {
 public:
  TandemDualUsbJoystick() {
    init_dual_report_descriptor();
    static bool registered = false;
    if (!registered) {
      registered = true;
      USBHID::addDevice(this, static_cast<uint16_t>(kDualReportDescriptorBytes));
    }
  }

  void begin() { hid_.begin(); }

  bool ready() { return hid_.ready(); }

  bool send(uint8_t report_id, const tandem::StickState& state) {
    if (!hid_.ready()) {
      return false;
    }
    const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
    return hid_.SendReport(report_id, report.bytes, sizeof(report.bytes), 10);
  }

  uint16_t _onGetDescriptor(uint8_t* buffer) override {
    std::memcpy(buffer, g_dual_report_descriptor, kDualReportDescriptorBytes);
    return static_cast<uint16_t>(kDualReportDescriptorBytes);
  }

 private:
  USBHID hid_{};
};

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

TandemDualUsbJoystick g_dual_joystick;
tandem::FailsafeMonitor g_monitors[2]{};

uint32_t g_last_usb_us = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_led_ms = 0;
bool g_led_state = false;
uint32_t g_usb_sent = 0;
uint32_t g_usb_dropped = 0;
int8_t g_last_rssi[2]{0, 0};

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

  g_dual_joystick.begin();
  USB.manufacturerName("T.A.N.D.E.M.");
  USB.productName("TANDEM Base Station");
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
  Serial.printf("USB HID    : Dual Joystick (ID 1=Left, ID 2=Right, %u-byte desc)\n",
                static_cast<unsigned>(kDualReportDescriptorBytes));

  const uint32_t now_ms = millis();
  g_last_usb_us = micros();
  g_last_status_ms = now_ms;
  g_last_led_ms = now_ms;
}

void loop() {
  const uint32_t now_us = micros();
  const uint32_t now_ms = millis();

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
        if (!was_connected && g_monitors[i].is_connected()) {
          // Resync link stats when a stick recovers from disconnect/reboot.
          portENTER_CRITICAL(&g_rx_mux);
          g_link_stats[i].reset();
          g_link_stats[i].record_seq(local_snap[i].seq);
          portEXIT_CRITICAL(&g_rx_mux);
        }
      }
      g_monitors[i].update(now_ms);
    }

    const uint8_t report_ids[2] = {kReportIdLeft, kReportIdRight};
    for (size_t i = 0; i < 2U; ++i) {
      if (g_dual_joystick.send(report_ids[i], g_monitors[i].state())) {
        ++g_usb_sent;
      } else {
        ++g_usb_dropped;
      }
    }
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

  if (static_cast<uint32_t>(now_ms - g_last_status_ms) >= kStatusIntervalMs) {
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

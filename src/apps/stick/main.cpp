#include <Arduino.h>

#include <cstdint>
#include <cstdio>

#include "boards/board_config.h"
#include "stick_mode.h"
#include "stick_transport.h"
#include "tandem/config_protocol.h"
#include "tandem/core_info.h"
#include "tandem/input_pipeline.h"
#include "tandem/synthetic_input.h"
#include "tandem/types.h"
#include "tandem_hal/config_nvs.h"
#include "tandem_hal/input_scanner.h"
#include "transport_ble_config.h"
#include "transport_ble_hid.h"
#include "transport_espnow_tx.h"
#include "transport_usb_hid.h"

#if TANDEM_CAP_NATIVE_USB && !ARDUINO_USB_CDC_ON_BOOT
#include <USB.h>
#include <USBCDC.h>
namespace {
USBCDC g_usb_serial;
}  // namespace
#ifdef Serial
#undef Serial
#endif
#define Serial g_usb_serial
#endif

#ifndef TANDEM_STICK_ID
#define TANDEM_STICK_ID 1
#endif

#ifndef TANDEM_ESPNOW_CHANNEL
#define TANDEM_ESPNOW_CHANNEL 1
#endif

#ifndef TANDEM_ESPNOW_PEER_MAC
#define TANDEM_ESPNOW_PEER_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#endif

namespace {

using tandem::stick::StickTransport;
using tandem::stick::StickTransportMode;

constexpr tandem::StickId kDefaultStickId = static_cast<tandem::StickId>(TANDEM_STICK_ID);
static_assert(kDefaultStickId == tandem::StickId::Left || kDefaultStickId == tandem::StickId::Right, "TANDEM_STICK_ID must be 1 (Left) or 2 (Right)");

constexpr uint8_t kEspNowChannel = static_cast<uint8_t>(TANDEM_ESPNOW_CHANNEL);
constexpr uint8_t kPeerMac[6] = TANDEM_ESPNOW_PEER_MAC;

constexpr uint32_t kStatusIntervalMs = 2000;
constexpr uint32_t kLiveStreamIntervalMs = 33;
constexpr uint32_t kLedSynthIntervalMs = 250;
constexpr uint32_t kLedLiveIntervalMs = 1000;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kLongPressMs = 1000;
constexpr uint32_t kUsbSerialWaitMs = 1500;

tandem::hal::InputScanner g_scanner;
tandem::hal::InputScannerConfig g_scanner_config{};
tandem::StickInputConfig g_input_config{};

#if TANDEM_CAP_NATIVE_USB
tandem::stick::UsbHidTransport g_usb_transport;
#endif
#if TANDEM_CAP_BLE
tandem::stick::BleHidTransport g_ble_transport;
tandem::stick::BleConfigTransport g_ble_config_transport;
#endif
#if TANDEM_CAP_ESPNOW
tandem::stick::EspNowTxTransport g_espnow_transport;
#endif

StickTransportMode g_mode = tandem::stick::default_transport_mode();
StickTransport* g_transport = nullptr;
tandem::StickId g_stick_id = kDefaultStickId;

#if defined(TANDEM_STICK_SYNTHETIC_DEFAULT)
bool g_synthetic_enabled = true;
#else
bool g_synthetic_enabled = false;
#endif

uint32_t g_last_tx_us = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_live_ms = 0;
uint32_t g_last_led_ms = 0;
uint32_t g_rate_window_ms = 0;
uint32_t g_sample_count = 0;
uint32_t g_tx_count = 0;
uint16_t g_measured_sample_hz = 0;
uint16_t g_measured_tx_hz = 0;
bool g_led_state = false;
bool g_live_serial = false;
bool g_live_ble = false;
bool g_live_webhid = false;

enum class ConfigChannel : uint8_t {
  Serial = 0,
  Ble = 1,
  WebHid = 2,
};

char g_cmd_buf[tandem::config::kMaxCommandLength]{};
size_t g_cmd_len = 0;
bool g_cmd_reading_line = false;

tandem::RawStickInput g_last_raw{};
tandem::StickState g_last_state = tandem::make_centered_state(tandem::kStateFlagConnected);

bool g_boot_stable_pressed = false;
bool g_boot_last_raw_pressed = false;
uint32_t g_boot_last_change_ms = 0;
uint32_t g_boot_press_start_ms = 0;
bool g_boot_long_fired = false;

StickTransport* transport_for_mode(StickTransportMode mode) noexcept {
  switch (mode) {
    case StickTransportMode::UsbHid:
#if TANDEM_CAP_NATIVE_USB
      return &g_usb_transport;
#else
      return nullptr;
#endif
    case StickTransportMode::BleHid:
#if TANDEM_CAP_BLE
      return &g_ble_transport;
#else
      return nullptr;
#endif
    case StickTransportMode::EspNowTx:
#if TANDEM_CAP_ESPNOW
      return &g_espnow_transport;
#else
      return nullptr;
#endif
    case StickTransportMode::BleConfig:
#if TANDEM_CAP_BLE
      return &g_ble_config_transport;
#else
      return nullptr;
#endif
  }
  return nullptr;
}

tandem::config::DeviceCapabilities build_capabilities() noexcept;

void emit_unsolicited_caps_if_streaming() noexcept {
  if (!g_live_serial && !g_live_webhid) {
    return;
  }
  char resp[tandem::config::kMaxResponseLength]{};
  const size_t n = tandem::config::format_caps_response(build_capabilities(), resp, sizeof(resp));
  if (n > 0) {
    if (g_live_serial) {
      Serial.print(resp);
    }
#if TANDEM_CAP_NATIVE_USB
    if (g_live_webhid) {
      g_usb_transport.send_config_response(0U, resp, n);
    }
#endif
  }
}

void switch_transport_mode(StickTransportMode target) noexcept {
  if (!tandem::stick::is_mode_supported(target)) {
    Serial.printf("[stick] ERROR: mode %s is not supported on %s\n", tandem::stick::mode_name(target), tandem::board::kBoardName);
    return;
  }
  // Allow retrying the same mode if its previous start() failed.
  if (target == g_mode && g_transport != nullptr && g_transport->is_active()) {
    Serial.printf("[stick] already in mode %s\n", tandem::stick::mode_name(g_mode));
    return;
  }
  // Strict mutual exclusion: tear down the current transport (including BLE/Wi-Fi radio)
  // before initializing the next transport. Note: BLE init/deinit blocks ~100-500 ms, so
  // mode switches must only be triggered by explicit user actions, never automatically.
  if (g_transport != nullptr) {
    g_transport->stop();
  }
  // BLE live streaming stops across radio teardown, while USB Serial stays connected.
  g_live_ble = false;
  g_mode = target;
  g_transport = transport_for_mode(g_mode);
  const bool ok = (g_transport != nullptr) && g_transport->start();
  g_last_tx_us = micros();
  g_rate_window_ms = millis();
  g_sample_count = 0;
  g_tx_count = 0;
  g_measured_sample_hz = 0;
  g_measured_tx_hz = 0;
  if (ok) {
    if (g_transport->device_name() != nullptr) {
      Serial.printf("[stick] mode -> %s (%s, ok)\n", tandem::stick::mode_name(g_mode), g_transport->device_name());
    } else {
      Serial.printf("[stick] mode -> %s (ok)\n", tandem::stick::mode_name(g_mode));
    }
    emit_unsolicited_caps_if_streaming();
  } else {
    Serial.printf("[stick] ERROR: failed to start mode %s\n", tandem::stick::mode_name(g_mode));
  }
}

tandem::config::TelemetrySnapshot build_telemetry() noexcept {
  tandem::config::TelemetrySnapshot tlm{};
  tlm.has_sample_hz = true;
  tlm.sample_hz = g_measured_sample_hz;
  tlm.has_tx_hz = true;
  tlm.tx_hz = g_measured_tx_hz;
  tlm.has_failsafe = true;
  tlm.failsafe_active = false;
  if (g_transport != nullptr) {
    g_transport->fill_telemetry(tlm);
  }
  return tlm;
}

tandem::config::DeviceCapabilities build_capabilities() noexcept {
  tandem::config::DeviceCapabilities caps{};
  caps.board = tandem::board::kBoardName;
  caps.stick_id = g_stick_id;
  caps.transport_mode = tandem::stick::mode_name(g_mode);
  caps.synthetic = g_synthetic_enabled;
  caps.adc_max = (1L << g_scanner_config.adc_bits) - 1L;
  caps.is_base_station = false;
  caps.has_config = true;
  caps.fw_version = tandem::core_version();
  caps.git_commit = tandem::core_git_commit();

  uint8_t transports_mask = tandem::config::kTransportMaskUsbSerial;
  if constexpr (tandem::board::kHasNativeUsb) {
    transports_mask |= tandem::config::kTransportMaskUsbHid | tandem::config::kTransportMaskWebHid;
  }
  if constexpr (tandem::board::kHasBle) {
    transports_mask |= tandem::config::kTransportMaskBleHid | tandem::config::kTransportMaskBleConfig;
  }
  if constexpr (tandem::board::kHasEspNow) {
    transports_mask |= tandem::config::kTransportMaskEspNowTx;
  }
  caps.transports_mask = transports_mask;

  const tandem::config::TelemetrySnapshot tlm = build_telemetry();
  caps.peer_status = tlm.has_peer_status ? tlm.peer_status : nullptr;

  uint8_t axes_mask = 0;
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    if (g_scanner_config.axis_pins[i] >= 0) {
      axes_mask |= static_cast<uint8_t>(1u << i);
    }
  }
  caps.axes_mask = axes_mask;

  uint32_t btns_mask = 0;
  for (uint8_t i = 0; i < tandem::hal::kMaxButtons; ++i) {
    if (g_scanner_config.button_pins[i] >= 0) {
      btns_mask |= (1UL << i);
    }
  }
  caps.buttons_mask = btns_mask;

  bool has_hat = false;
  for (uint8_t i = 0; i < tandem::hal::kHatPinCount; ++i) {
    if (g_scanner_config.hat_pins[i] >= 0) {
      has_hat = true;
      break;
    }
  }
  caps.has_hat = has_hat;
  return caps;
}

void handle_config_line(const char* line, ConfigChannel channel = ConfigChannel::Serial, uint8_t seq = 0) noexcept {
#if !TANDEM_CAP_NATIVE_USB
  (void)seq;
#endif
  struct CallbackContext {
    ConfigChannel channel;
  } cb_ctx{channel};

  tandem::config::ConfigCallbacks cbs{};
  cbs.ctx = &cb_ctx;
  cbs.on_live_changed = [](void* ctx, bool enabled) {
    const auto ch = (ctx != nullptr) ? static_cast<const CallbackContext*>(ctx)->channel : ConfigChannel::Serial;
    if (ch == ConfigChannel::Ble) {
      g_live_ble = enabled;
    } else if (ch == ConfigChannel::WebHid) {
      g_live_webhid = enabled;
    } else {
      g_live_serial = enabled;
    }
    g_last_live_ms = millis();
  };
  cbs.on_save = [](void*) -> bool { return tandem::hal::save_stick_config(g_input_config); };
  cbs.on_reset = [](void*) -> bool {
    g_input_config = tandem::hal::make_default_input_config(g_scanner_config);
    return tandem::hal::reset_stick_config();
  };
  cbs.on_get_telemetry = [](void*, tandem::config::TelemetrySnapshot& out) -> bool {
    out = build_telemetry();
    return true;
  };

  char resp[tandem::config::kMaxResponseLength]{};
  const size_t n = tandem::config::process_command(line, build_capabilities(), g_input_config, cbs, resp, sizeof(resp));
  if (n > 0) {
    if (channel == ConfigChannel::Ble) {
#if TANDEM_CAP_BLE
      g_ble_config_transport.send_response(resp, n);
#endif
    } else if (channel == ConfigChannel::WebHid) {
#if TANDEM_CAP_NATIVE_USB
      g_usb_transport.send_config_response(seq, resp, n);
#endif
    } else {
      Serial.print(resp);
    }
  }
}

void poll_controls(uint32_t now_ms) noexcept {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (g_cmd_reading_line) {
      if (ch == '\r' || ch == '\n') {
        g_cmd_buf[g_cmd_len] = '\0';
        g_cmd_reading_line = false;
        g_cmd_len = 0;
        handle_config_line(g_cmd_buf);
      } else if (g_cmd_len + 1 < sizeof(g_cmd_buf)) {
        g_cmd_buf[g_cmd_len++] = static_cast<char>(ch);
      } else {
        g_cmd_reading_line = false;
        g_cmd_len = 0;
        Serial.printf("%s{\"v\":%u,\"cmd\":\"unknown\",\"ok\":false,\"err\":\"line_too_long\"}\n", tandem::config::kResponsePrefix,
                      static_cast<unsigned>(tandem::config::kProtocolVersion));
      }
      continue;
    }

    if (ch == '$') {
      g_cmd_reading_line = true;
      g_cmd_buf[0] = '$';
      g_cmd_len = 1;
      continue;
    }

    if (ch == 't' || ch == 'T') {
      g_synthetic_enabled = !g_synthetic_enabled;
      Serial.printf("[stick] input -> %s\n", g_synthetic_enabled ? "SYNTHETIC" : "LIVE");
    } else if (ch == 'm' || ch == 'M') {
      switch_transport_mode(tandem::stick::next_supported_mode(g_mode));
    } else if (ch == '1') {
      switch_transport_mode(StickTransportMode::UsbHid);
    } else if (ch == '2') {
      switch_transport_mode(StickTransportMode::BleHid);
    } else if (ch == '3') {
      switch_transport_mode(StickTransportMode::EspNowTx);
    } else if (ch == '4') {
      switch_transport_mode(StickTransportMode::BleConfig);
    } else if (ch == 'i' || ch == 'I') {
      g_stick_id = (g_stick_id == tandem::StickId::Left) ? tandem::StickId::Right : tandem::StickId::Left;
      if (g_transport != nullptr) {
        g_transport->on_stick_id_changed();
      }
      Serial.printf("[stick] id -> %s\n", (g_stick_id == tandem::StickId::Left) ? "LEFT" : "RIGHT");
      emit_unsolicited_caps_if_streaming();
    } else if (ch != '\r' && ch != '\n') {
      Serial.printf("[stick] ERROR: unknown command '%c' (use t/m/1/2/3/4/i)\n", (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?');
    }
  }

#if TANDEM_CAP_BLE
  if (g_mode == StickTransportMode::BleConfig) {
    char ble_cmd[tandem::config::kMaxCommandLength]{};
    bool overflow = false;
    while (g_ble_config_transport.pop_command(ble_cmd, sizeof(ble_cmd), overflow)) {
      if (overflow) {
        char err_resp[128]{};
        const int n = std::snprintf(err_resp, sizeof(err_resp), "%s{\"v\":%u,\"cmd\":\"unknown\",\"ok\":false,\"err\":\"line_too_long\"}\n",
                                    tandem::config::kResponsePrefix, static_cast<unsigned>(tandem::config::kProtocolVersion));
        if (n > 0) {
          g_ble_config_transport.send_response(err_resp, static_cast<size_t>(n));
        }
      } else {
        handle_config_line(ble_cmd, ConfigChannel::Ble);
      }
    }
  }
#endif

#if TANDEM_CAP_NATIVE_USB
  g_usb_transport.drain_config_commands([](const char* hid_cmd, uint8_t hid_seq) { handle_config_line(hid_cmd, ConfigChannel::WebHid, hid_seq); });
#endif

  const bool raw_pressed = (digitalRead(tandem::board::kPinBootButton) == LOW);
  if (raw_pressed != g_boot_last_raw_pressed) {
    g_boot_last_raw_pressed = raw_pressed;
    g_boot_last_change_ms = now_ms;
  } else if (raw_pressed != g_boot_stable_pressed && static_cast<uint32_t>(now_ms - g_boot_last_change_ms) >= kButtonDebounceMs) {
    g_boot_stable_pressed = raw_pressed;
    if (g_boot_stable_pressed) {
      g_boot_press_start_ms = now_ms;
      g_boot_long_fired = false;
    } else if (!g_boot_long_fired) {
      g_synthetic_enabled = !g_synthetic_enabled;
      Serial.printf("[stick] BOOT short -> input %s\n", g_synthetic_enabled ? "SYNTHETIC" : "LIVE");
    }
  }

  if (g_boot_stable_pressed && !g_boot_long_fired && static_cast<uint32_t>(now_ms - g_boot_press_start_ms) >= kLongPressMs) {
    g_boot_long_fired = true;
    switch_transport_mode(tandem::stick::next_supported_mode(g_mode));
  }
}

void print_status(uint32_t now_ms) noexcept {
  char stats[96]{};
  if (g_transport != nullptr) {
    g_transport->format_stats(stats, sizeof(stats));
  }
  Serial.printf("[%lu ms] mode=%s stick=%c in=%s heap=%lu %s | raw=[%ld,%ld,%ld,%ld] out=[%d,%d,%d,%d] btns=0x%08lX hat=%u\n",
                static_cast<unsigned long>(now_ms), tandem::stick::mode_name(g_mode), (g_stick_id == tandem::StickId::Left) ? 'L' : 'R',
                g_synthetic_enabled ? "SYNTH" : "LIVE", static_cast<unsigned long>(ESP.getFreeHeap()), stats, static_cast<long>(g_last_raw.axes[0]),
                static_cast<long>(g_last_raw.axes[1]), static_cast<long>(g_last_raw.axes[2]), static_cast<long>(g_last_raw.axes[3]),
                static_cast<int>(g_last_state.axes[0]), static_cast<int>(g_last_state.axes[1]), static_cast<int>(g_last_state.axes[2]),
                static_cast<int>(g_last_state.axes[3]), static_cast<unsigned long>(g_last_state.buttons), static_cast<unsigned>(g_last_state.hat));
}

}  // namespace

void setup() {
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    pinMode(tandem::board::kPinStatusLed, OUTPUT);
    digitalWrite(tandem::board::kPinStatusLed, LOW);
  }
  pinMode(tandem::board::kPinBootButton, INPUT_PULLUP);

  g_scanner_config = tandem::board::make_default_scanner_config();
  g_scanner.init(g_scanner_config);
  g_input_config = tandem::hal::make_default_input_config(g_scanner_config);
  const tandem::hal::NvsLoadStatus nvs_status = tandem::hal::load_stick_config(g_input_config);

#if TANDEM_CAP_NATIVE_USB
  // Initialize USB composite CDC+HID once at boot so Serial remains available
  // on native-USB targets even if wireless transport mode is selected.
  g_usb_transport.init_usb_stack();
#endif
#if TANDEM_CAP_ESPNOW
  g_espnow_transport.configure(kEspNowChannel, kPeerMac);
#endif

#if TANDEM_CAP_NATIVE_USB
  tandem::stick::UsbHidTransport::configure_usb_identity();
#endif

  Serial.begin(115200);
#if TANDEM_CAP_NATIVE_USB
#if !ARDUINO_USB_CDC_ON_BOOT
  USB.begin();
#endif
  const uint32_t wait_start_ms = millis();
  while (!Serial && static_cast<uint32_t>(millis() - wait_start_ms) < kUsbSerialWaitMs) {
    delay(10);
  }
#else
  delay(200);
#endif

  g_transport = transport_for_mode(g_mode);
  const bool ok = (g_transport != nullptr) && g_transport->start();

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. Unified Stick Firmware ===");
  Serial.printf("Board      : %s\n", tandem::board::kBoardName);
  Serial.printf("Caps       : USB=%d BLE=%d ESP-NOW=%d\n", tandem::board::kHasNativeUsb ? 1 : 0, tandem::board::kHasBle ? 1 : 0,
                tandem::board::kHasEspNow ? 1 : 0);
  Serial.printf("Mode       : %s (%s)\n", tandem::stick::mode_name(g_mode), ok ? "ok" : "FAILED");
  if (ok && g_transport != nullptr && g_transport->device_name() != nullptr) {
    Serial.printf("BLE Name   : %s\n", g_transport->device_name());
  }
  Serial.printf("Stick ID   : %s\n", (g_stick_id == tandem::StickId::Left) ? "LEFT (1)" : "RIGHT (2)");
  Serial.printf("Input      : %s\n", g_synthetic_enabled ? "SYNTHETIC" : "LIVE");
  Serial.printf("Config NVS : %s\n", tandem::hal::nvs_load_status_name(nvs_status));
  Serial.println("Controls   : 't'/BOOT-short = LIVE<->SYNTH | 'm'/BOOT-long = next mode | '1'/'2'/'3'/'4' = USB/BLE/ESP-NOW/BLE-CFG | 'i' = L<->R");

  g_last_tx_us = micros();
  g_last_status_ms = millis();
  g_last_led_ms = g_last_status_ms;
  g_rate_window_ms = g_last_status_ms;
}

void loop() {
  const uint32_t now_ms = millis();

  poll_controls(now_ms);

  // Sample micros() after poll_controls() so a mode switch doesn't cause unsigned underflow.
  const uint32_t now_us = micros();

  if (g_transport != nullptr) {
    g_transport->poll();

    if (static_cast<uint32_t>(now_us - g_last_tx_us) >= g_transport->interval_us()) {
      g_last_tx_us = now_us;

      if (g_synthetic_enabled) {
        g_last_state = tandem::make_synthetic_stick_state(now_ms, g_stick_id);
      } else {
        g_scanner.sample(now_ms, g_last_raw);
        g_last_state = tandem::process_stick_input(g_last_raw, g_input_config, tandem::kStateFlagConnected);
      }
      ++g_sample_count;

      if (g_transport->send(g_stick_id, g_last_state)) {
        ++g_tx_count;
      }
    }
  }

  const uint32_t rate_elapsed_ms = static_cast<uint32_t>(now_ms - g_rate_window_ms);
  if (rate_elapsed_ms >= 1000U) {
    g_measured_sample_hz = static_cast<uint16_t>((g_sample_count * 1000UL) / rate_elapsed_ms);
    g_measured_tx_hz = static_cast<uint16_t>((g_tx_count * 1000UL) / rate_elapsed_ms);
    g_sample_count = 0;
    g_tx_count = 0;
    g_rate_window_ms = now_ms;
  }

  const uint32_t led_interval = g_synthetic_enabled ? kLedSynthIntervalMs : kLedLiveIntervalMs;
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    if (static_cast<uint32_t>(now_ms - g_last_led_ms) >= led_interval) {
      g_last_led_ms = now_ms;
      g_led_state = !g_led_state;
      digitalWrite(tandem::board::kPinStatusLed, g_led_state ? HIGH : LOW);
    }
  }

  bool ble_live_active = false;
#if TANDEM_CAP_BLE
  if (g_mode != StickTransportMode::BleConfig || !g_ble_config_transport.connected()) {
    g_live_ble = false;
  }
  ble_live_active = g_live_ble;
#endif

  bool webhid_live_active = false;
#if TANDEM_CAP_NATIVE_USB
  webhid_live_active = g_live_webhid;
#endif

  if (g_live_serial || ble_live_active || webhid_live_active) {
    if (static_cast<uint32_t>(now_ms - g_last_live_ms) >= kLiveStreamIntervalMs) {
      g_last_live_ms = now_ms;
      char resp[tandem::config::kMaxResponseLength]{};
      const size_t n = tandem::config::format_live_response(g_last_raw, g_last_state, g_synthetic_enabled, build_telemetry(), resp, sizeof(resp));
      if (n > 0) {
        if (g_live_serial) {
          Serial.print(resp);
        }
#if TANDEM_CAP_BLE
        if (ble_live_active) {
          g_ble_config_transport.send_live(resp, n);
        }
#endif
#if TANDEM_CAP_NATIVE_USB
        if (webhid_live_active) {
          g_usb_transport.send_config_live(resp, n);
        }
#endif
      }
    }
  } else if (static_cast<uint32_t>(now_ms - g_last_status_ms) >= kStatusIntervalMs) {
    g_last_status_ms = now_ms;
    print_status(now_ms);
  }

#if TANDEM_CAP_NATIVE_USB
  bool webhid_stalled = false;
  g_usb_transport.poll_config_tx(now_ms, webhid_stalled);
  if (webhid_stalled) {
    g_live_webhid = false;
  }
#endif
}

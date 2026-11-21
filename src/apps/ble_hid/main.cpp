#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "boards/board_config.h"
#include "tandem/axis_shaping.h"
#include "tandem/hid_report.h"
#include "tandem/input_pipeline.h"
#include "tandem/types.h"
#include "tandem_hal/input_scanner.h"

static_assert(tandem::board::kHasBle, "ble_hid requires a board with BLE support");

namespace {

constexpr uint32_t kReportIntervalMs = 10;    // 100 Hz BLE HID notification target
constexpr uint32_t kStatusIntervalMs = 2000;  // Throttled serial telemetry
constexpr uint32_t kLedSynthIntervalMs = 250;
constexpr uint32_t kLedLiveIntervalMs = 1000;
constexpr uint8_t kBootButtonPin = 0;         // ESP32 BOOT/GPIO0 (active-low)
constexpr uint32_t kButtonDebounceMs = 30;
constexpr float kDefaultDeadzone = 0.05f;
constexpr float kTwoPi = 6.28318530718f;

constexpr const char* kDeviceName = "TANDEM-BLE-Stick";
constexpr const char* kManufacturer = "T.A.N.D.E.M.";
constexpr uint16_t kBleMtuBytes = 64;  // >= 24 bytes so 21-byte reports fit in one notification

std::atomic<bool> g_connected{false};
std::atomic<bool> g_restart_advertising{false};

class ServerCallbacks final : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer*) override {
    g_connected.store(true, std::memory_order_release);
  }

  void onDisconnect(BLEServer*) override {
    g_connected.store(false, std::memory_order_release);
    g_restart_advertising.store(true, std::memory_order_release);
  }
};

ServerCallbacks g_server_callbacks;
BLEHIDDevice* g_hid = nullptr;
BLECharacteristic* g_input_report = nullptr;

// Static copy because BLEHIDDevice::reportMap() takes a non-const uint8_t* pointer.
uint8_t g_report_map[tandem::kHidJoystickReportDescriptorBytes]{};

tandem::hal::InputScanner g_scanner;
tandem::hal::InputScannerConfig g_scanner_config{};
tandem::StickInputConfig g_input_config{};

#if defined(TANDEM_BLE_HID_SYNTHETIC_DEFAULT)
bool g_synthetic_enabled = true;
#else
bool g_synthetic_enabled = false;
#endif

uint32_t g_last_report_ms = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_led_ms = 0;
bool g_led_state = false;
uint32_t g_sent_reports = 0;
tandem::RawStickInput g_last_raw{};
tandem::StickState g_last_state = tandem::make_centered_state(tandem::kStateFlagConnected);

bool g_boot_stable_pressed = false;
bool g_boot_last_raw_pressed = false;
uint32_t g_boot_last_change_ms = 0;

tandem::StickInputConfig make_default_input_config(
    const tandem::hal::InputScannerConfig& scanner_cfg) noexcept {
  tandem::StickInputConfig cfg{};
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    if (scanner_cfg.axis_pins[i] >= 0) {
      cfg.axes[i].calibration = tandem::AxisCalibration{};
      cfg.axes[i].deadzone = kDefaultDeadzone;
      cfg.axes[i].curve = tandem::make_linear_curve();
      cfg.axes[i].invert = false;
      cfg.axes[i].enabled = true;
    }
  }
  return cfg;
}

int16_t sine_axis(float phase_rad) noexcept {
  const float scaled = std::sin(phase_rad) * static_cast<float>(tandem::kAxisMax);
  const long rounded = std::lround(scaled);
  if (rounded < tandem::kAxisMin) {
    return tandem::kAxisMin;
  }
  if (rounded > tandem::kAxisMax) {
    return tandem::kAxisMax;
  }
  return static_cast<int16_t>(rounded);
}

tandem::StickState make_synthetic_state(uint32_t now_ms) noexcept {
  tandem::StickState state = tandem::make_centered_state(tandem::kStateFlagConnected);

  // 4-second full sweep period; phase-offset each of the 8 axes for clear visibility.
  const float base_phase =
      kTwoPi * (static_cast<float>(now_ms % 4000U) / 4000.0f);
  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    const float offset =
        static_cast<float>(i) * (kTwoPi / static_cast<float>(tandem::kMaxAxes));
    state.axes[i] = sine_axis(base_phase + offset);
  }

  // Cycle through all 32 buttons (250 ms each) and 8-way POV hat 0..8 (500 ms each).
  const uint8_t btn_idx = static_cast<uint8_t>((now_ms / 250U) % 32U);
  state.buttons = (uint32_t{1} << btn_idx);
  state.hat = static_cast<uint8_t>((now_ms / 500U) % 9U);
  return state;
}

void setup_ble_hid() {
  BLEDevice::init(kDeviceName);
  BLEDevice::setMTU(kBleMtuBytes);

  // Just-Works Secure Connections + bonding required by Windows/macOS BLE HID hosts.
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(&g_server_callbacks);
  server->advertiseOnDisconnect(false);

  g_hid = new BLEHIDDevice(server);
  g_input_report = g_hid->inputReport(tandem::kHidReportIdJoystick);

  g_hid->manufacturer()->setValue(kManufacturer);
  g_hid->pnp(0x02, 0xE502, 0xA111, 0x0100);
  g_hid->hidInfo(0x00, 0x01);

  std::memcpy(g_report_map,
              tandem::kHidJoystickReportDescriptor,
              tandem::kHidJoystickReportDescriptorBytes);
  g_hid->reportMap(g_report_map,
                   static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes));

  g_hid->startServices();
  g_hid->setBatteryLevel(100);

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setAppearance(HID_JOYSTICK);
  advertising->addServiceUUID(g_hid->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);  // Request fast 7.5 ms min connection interval
  BLEDevice::startAdvertising();
}

void poll_toggle_inputs(uint32_t now_ms) {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == 't' || ch == 'T') {
      g_synthetic_enabled = !g_synthetic_enabled;
      Serial.printf("[ble_hid] mode -> %s\n",
                    g_synthetic_enabled ? "SYNTHETIC" : "LIVE");
    }
  }

  const bool raw_pressed = (digitalRead(kBootButtonPin) == LOW);
  if (raw_pressed != g_boot_last_raw_pressed) {
    g_boot_last_raw_pressed = raw_pressed;
    g_boot_last_change_ms = now_ms;
  } else if (raw_pressed != g_boot_stable_pressed &&
             static_cast<uint32_t>(now_ms - g_boot_last_change_ms) >= kButtonDebounceMs) {
    g_boot_stable_pressed = raw_pressed;
    if (g_boot_stable_pressed) {
      g_synthetic_enabled = !g_synthetic_enabled;
      Serial.printf("[ble_hid] BOOT button -> mode %s\n",
                    g_synthetic_enabled ? "SYNTHETIC" : "LIVE");
    }
  }
}

}  // namespace

void setup() {
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    pinMode(tandem::board::kPinStatusLed, OUTPUT);
    digitalWrite(tandem::board::kPinStatusLed, LOW);
  }
  pinMode(kBootButtonPin, INPUT_PULLUP);

  g_scanner_config = tandem::board::make_default_scanner_config();
  g_scanner.init(g_scanner_config);
  g_input_config = make_default_input_config(g_scanner_config);

  Serial.begin(115200);
  delay(200);

  setup_ble_hid();

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. BLE HID Joystick (Mode A) ===");
  Serial.printf("Board      : %s\n", tandem::board::kBoardName);
  Serial.printf("Device     : %s (MTU=%u)\n",
                kDeviceName,
                static_cast<unsigned>(kBleMtuBytes));
  Serial.printf("Descriptor : %u bytes (Report ID %u, Payload %u bytes)\n",
                static_cast<unsigned>(tandem::kHidJoystickReportDescriptorBytes),
                static_cast<unsigned>(tandem::kHidReportIdJoystick),
                static_cast<unsigned>(tandem::kHidJoystickReportBytes));
  Serial.printf("Mode       : %s (press BOOT/GPIO0 or send 't' to toggle)\n",
                g_synthetic_enabled ? "SYNTHETIC" : "LIVE");

  const uint32_t now_ms = millis();
  g_last_report_ms = now_ms;
  g_last_status_ms = now_ms;
  g_last_led_ms = now_ms;
}

void loop() {
  const uint32_t now_ms = millis();

  poll_toggle_inputs(now_ms);

  if (g_restart_advertising.exchange(false, std::memory_order_acq_rel)) {
    g_last_raw = tandem::RawStickInput{};
    g_last_state = tandem::make_centered_state(tandem::kStateFlagNone);
    BLEDevice::startAdvertising();
    Serial.println("[ble_hid] disconnected -> advertising restarted");
  }

  if (static_cast<uint32_t>(now_ms - g_last_report_ms) >= kReportIntervalMs) {
    g_last_report_ms = now_ms;

    // Sample continuously even while disconnected so debounce state and first
    // post-reconnect report are always fresh (never stale pre-disconnect values).
    if (g_synthetic_enabled) {
      g_last_state = make_synthetic_state(now_ms);
    } else {
      g_scanner.sample(now_ms, g_last_raw);
      g_last_state = tandem::process_stick_input(
          g_last_raw, g_input_config, tandem::kStateFlagConnected);
    }

    if (g_connected.load(std::memory_order_acquire) && g_input_report != nullptr) {
      const tandem::HidJoystickReport report =
          tandem::encode_hid_joystick_report(g_last_state);
      g_input_report->setValue(report.bytes, sizeof(report.bytes));
      g_input_report->notify();
      ++g_sent_reports;
    }
  }

  const uint32_t led_interval =
      g_synthetic_enabled ? kLedSynthIntervalMs : kLedLiveIntervalMs;
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    if (static_cast<uint32_t>(now_ms - g_last_led_ms) >= led_interval) {
      g_last_led_ms = now_ms;
      g_led_state = !g_led_state;
      digitalWrite(tandem::board::kPinStatusLed, g_led_state ? HIGH : LOW);
    }
  }

  if (static_cast<uint32_t>(now_ms - g_last_status_ms) >= kStatusIntervalMs) {
    g_last_status_ms = now_ms;
    Serial.printf(
        "[%lu ms] mode=%s conn=%d sent=%lu | raw=[%ld,%ld,%ld,%ld] out=[%d,%d,%d,%d] btns=0x%08lX hat=%u\n",
        static_cast<unsigned long>(now_ms),
        g_synthetic_enabled ? "SYNTH" : "LIVE",
        g_connected.load(std::memory_order_acquire) ? 1 : 0,
        static_cast<unsigned long>(g_sent_reports),
        static_cast<long>(g_last_raw.axes[0]),
        static_cast<long>(g_last_raw.axes[1]),
        static_cast<long>(g_last_raw.axes[2]),
        static_cast<long>(g_last_raw.axes[3]),
        static_cast<int>(g_last_state.axes[0]),
        static_cast<int>(g_last_state.axes[1]),
        static_cast<int>(g_last_state.axes[2]),
        static_cast<int>(g_last_state.axes[3]),
        static_cast<unsigned long>(g_last_state.buttons),
        static_cast<unsigned>(g_last_state.hat));
  }
}

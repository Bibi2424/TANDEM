#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>
#include <cstdint>
#include <cstring>

#include "boards/board_config.h"
#include "tandem/axis_shaping.h"
#include "tandem/hid_report.h"
#include "tandem/input_pipeline.h"
#include "tandem/synthetic_input.h"
#include "tandem/types.h"
#include "tandem_hal/input_scanner.h"

namespace {

constexpr uint32_t kReportIntervalUs = 1000;   // 1000 Hz USB HID polling target
constexpr uint32_t kStatusIntervalMs = 2000;   // Throttled serial status
constexpr uint32_t kLedBlinkIntervalMs = 250;  // Visual liveness
constexpr uint32_t kUsbSerialWaitMs = 1500;
constexpr uint8_t kBootButtonPin = 0;          // LOLIN S2 Mini BOOT/GPIO0 (active-low)
constexpr uint32_t kButtonDebounceMs = 30;

class TandemUsbJoystick final : public USBHIDDevice {
 public:
  TandemUsbJoystick() {
    static bool registered = false;
    if (!registered) {
      registered = true;
      USBHID::addDevice(
          this, static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes));
    }
  }

  void begin() { hid_.begin(); }

  bool ready() { return hid_.ready(); }

  bool send(const tandem::StickState& state) {
    if (!hid_.ready()) {
      return false;
    }
    const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
    // Bounded timeout: wakes immediately via tud_hid_report_complete_cb() when USB is active,
    // and ready() guard skips SendReport entirely when USB is unmounted/busy.
    return hid_.SendReport(tandem::kHidReportIdJoystick, report.bytes, sizeof(report.bytes), 10);
  }

  uint16_t _onGetDescriptor(uint8_t* buffer) override {
    std::memcpy(buffer,
                tandem::kHidJoystickReportDescriptor,
                tandem::kHidJoystickReportDescriptorBytes);
    return static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes);
  }

 private:
  USBHID hid_{};
};

TandemUsbJoystick g_joystick;
tandem::hal::InputScanner g_scanner;
tandem::hal::InputScannerConfig g_scanner_config{};
tandem::StickInputConfig g_input_config{};

#if defined(TANDEM_USB_HID_SYNTHETIC_DEFAULT)
bool g_synthetic_enabled = true;
#else
bool g_synthetic_enabled = false;
#endif

uint32_t g_last_report_us = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_led_ms = 0;
bool g_led_state = false;

uint32_t g_sent_reports = 0;
uint32_t g_dropped_reports = 0;
tandem::RawStickInput g_last_raw{};
tandem::StickState g_last_state = tandem::make_centered_state(tandem::kStateFlagConnected);

bool g_boot_stable_pressed = false;
bool g_boot_last_raw_pressed = false;
uint32_t g_boot_last_change_ms = 0;

void poll_toggle_inputs(uint32_t now_ms) {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == 't' || ch == 'T') {
      g_synthetic_enabled = !g_synthetic_enabled;
      Serial.printf("[usb_hid] mode -> %s\n",
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
      Serial.printf("[usb_hid] BOOT button -> mode %s\n",
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
  g_input_config = tandem::hal::make_default_input_config(g_scanner_config);

  g_joystick.begin();
  USB.begin();

  Serial.begin(115200);
  const uint32_t wait_start_ms = millis();
  while (!Serial && static_cast<uint32_t>(millis() - wait_start_ms) < kUsbSerialWaitMs) {
    delay(10);
  }
  delay(100);

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. USB HID Joystick (Mode C) ===");
  Serial.printf("Board      : %s\n", tandem::board::kBoardName);
  Serial.printf("Descriptor : %u bytes (Report ID %u, Payload %u bytes)\n",
                static_cast<unsigned>(tandem::kHidJoystickReportDescriptorBytes),
                static_cast<unsigned>(tandem::kHidReportIdJoystick),
                static_cast<unsigned>(tandem::kHidJoystickReportBytes));
  Serial.printf("Mode       : %s (press BOOT/GPIO0 or send 't' to toggle)\n",
                g_synthetic_enabled ? "SYNTHETIC" : "LIVE");

  const uint32_t now_ms = millis();
  g_last_report_us = micros();
  g_last_status_ms = now_ms;
  g_last_led_ms = now_ms;
}

void loop() {
  const uint32_t now_us = micros();
  const uint32_t now_ms = millis();

  poll_toggle_inputs(now_ms);

  if (static_cast<uint32_t>(now_us - g_last_report_us) >= kReportIntervalUs) {
    g_last_report_us = now_us;

    if (g_synthetic_enabled) {
      g_last_state = tandem::make_synthetic_stick_state(now_ms);
    } else {
      g_scanner.sample(now_ms, g_last_raw);
      g_last_state = tandem::process_stick_input(
          g_last_raw, g_input_config, tandem::kStateFlagConnected);
    }

    if (g_joystick.send(g_last_state)) {
      ++g_sent_reports;
    } else {
      ++g_dropped_reports;
    }
  }

  const uint32_t led_interval = g_synthetic_enabled ? kLedBlinkIntervalMs : 1000U;
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
        "[%lu ms] mode=%s ready=%d sent=%lu drop=%lu | raw=[%ld,%ld,%ld,%ld] out=[%d,%d,%d,%d] btns=0x%08lX hat=%u\n",
        static_cast<unsigned long>(now_ms),
        g_synthetic_enabled ? "SYNTH" : "LIVE",
        g_joystick.ready() ? 1 : 0,
        static_cast<unsigned long>(g_sent_reports),
        static_cast<unsigned long>(g_dropped_reports),
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

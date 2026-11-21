#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "boards/board_config.h"
#include "tandem/hid_report.h"
#include "tandem/types.h"

namespace {

constexpr uint32_t kReportIntervalUs = 1000;   // 1000 Hz USB HID polling target
constexpr uint32_t kStatusIntervalMs = 2000;   // Throttled serial status
constexpr uint32_t kLedBlinkIntervalMs = 250;  // Visual liveness
constexpr uint32_t kUsbSerialWaitMs = 1500;
constexpr uint8_t kBootButtonPin = 0;          // LOLIN S2 Mini BOOT/GPIO0 (active-low)
constexpr uint32_t kButtonDebounceMs = 30;
constexpr float kTwoPi = 6.28318530718f;

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

bool g_synthetic_enabled = true;
uint32_t g_last_report_us = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_led_ms = 0;
bool g_led_state = false;

uint32_t g_sent_reports = 0;
uint32_t g_dropped_reports = 0;

bool g_boot_stable_pressed = false;
bool g_boot_last_raw_pressed = false;
uint32_t g_boot_last_change_ms = 0;

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
    const float offset = static_cast<float>(i) * (kTwoPi / static_cast<float>(tandem::kMaxAxes));
    state.axes[i] = sine_axis(base_phase + offset);
  }

  // Cycle through all 32 buttons (250 ms each) and 8-way POV hat 0..8 (500 ms each).
  const uint8_t btn_idx = static_cast<uint8_t>((now_ms / 250U) % 32U);
  state.buttons = (uint32_t{1} << btn_idx);
  state.hat = static_cast<uint8_t>((now_ms / 500U) % 9U);
  return state;
}

void poll_toggle_inputs(uint32_t now_ms) {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == 't' || ch == 'T') {
      g_synthetic_enabled = !g_synthetic_enabled;
      Serial.printf("[usb_hid] synthetic mode %s\n",
                    g_synthetic_enabled ? "ON" : "OFF (centered)");
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
      Serial.printf("[usb_hid] BOOT button -> synthetic mode %s\n",
                    g_synthetic_enabled ? "ON" : "OFF (centered)");
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

  g_joystick.begin();
  USB.begin();

  Serial.begin(115200);
  const uint32_t wait_start_ms = millis();
  while (!Serial && static_cast<uint32_t>(millis() - wait_start_ms) < kUsbSerialWaitMs) {
    delay(10);
  }
  delay(100);

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. USB HID Prototype (Mode C) ===");
  Serial.printf("Board      : %s\n", tandem::board::kBoardName);
  Serial.printf("Descriptor : %u bytes (Report ID %u, Payload %u bytes)\n",
                static_cast<unsigned>(tandem::kHidJoystickReportDescriptorBytes),
                static_cast<unsigned>(tandem::kHidReportIdJoystick),
                static_cast<unsigned>(tandem::kHidJoystickReportBytes));
  Serial.println("Controls   : Press BOOT (GPIO0) or send 't' to toggle synthetic sweep");

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
    const tandem::StickState state =
        g_synthetic_enabled ? make_synthetic_state(now_ms)
                            : tandem::make_centered_state(tandem::kStateFlagConnected);
    if (g_joystick.send(state)) {
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
    Serial.printf("[%lu ms] ready=%d synth=%d sent=%lu drop=%lu\n",
                  static_cast<unsigned long>(now_ms),
                  g_joystick.ready() ? 1 : 0,
                  g_synthetic_enabled ? 1 : 0,
                  static_cast<unsigned long>(g_sent_reports),
                  static_cast<unsigned long>(g_dropped_reports));
  }
}

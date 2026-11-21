#include <Arduino.h>
#include <cstdint>
#include <limits>

#include "boards/board_config.h"
#include "tandem/input_pipeline.h"
#include "tandem_hal/input_scanner.h"

namespace {

constexpr uint32_t kSampleIntervalMs = 10;   // 100 Hz polling
constexpr uint32_t kReportIntervalMs = 500;  // 2 Hz serial summary
constexpr uint32_t kUsbSerialWaitMs = 1500;

struct AxisJitterStats {
  int32_t min_val{std::numeric_limits<int32_t>::max()};
  int32_t max_val{std::numeric_limits<int32_t>::min()};
  int32_t last_val{0};

  void record(int32_t val) noexcept {
    last_val = val;
    if (val < min_val) {
      min_val = val;
    }
    if (val > max_val) {
      max_val = val;
    }
  }

  void reset() noexcept {
    min_val = std::numeric_limits<int32_t>::max();
    max_val = std::numeric_limits<int32_t>::min();
  }

  int32_t peak_to_peak() const noexcept {
    if (max_val < min_val) {
      return 0;
    }
    return max_val - min_val;
  }
};

tandem::hal::InputScanner g_scanner;
tandem::hal::InputScannerConfig g_config;
AxisJitterStats g_axis_stats[tandem::kMaxAxes];

uint32_t g_last_sample_ms = 0;
uint32_t g_last_report_ms = 0;
bool g_led_state = false;

}  // namespace

void setup() {
  if constexpr (tandem::board::kPinStatusLed >= 0) {
    pinMode(tandem::board::kPinStatusLed, OUTPUT);
    digitalWrite(tandem::board::kPinStatusLed, LOW);
  }

  Serial.begin(115200);
  const uint32_t wait_start_ms = millis();
  while (!Serial && static_cast<uint32_t>(millis() - wait_start_ms) < kUsbSerialWaitMs) {
    delay(10);
  }
  delay(100);

  g_config = tandem::board::make_default_scanner_config();
  g_scanner.init(g_config);

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. Input Diagnostic ===");
  Serial.printf("Board        : %s\n", tandem::board::kBoardName);
  Serial.printf("ADC          : %u-bit, %ux oversample\n",
                static_cast<unsigned>(g_config.adc_bits),
                static_cast<unsigned>(g_config.adc_oversample));
  Serial.printf("Axis pins    : X=%d Y=%d Z=%d Thr=%d\n",
                g_config.axis_pins[0],
                g_config.axis_pins[1],
                g_config.axis_pins[2],
                g_config.axis_pins[3]);
  Serial.printf("Button pins  : [%d, %d, %d, %d] (active-low, %u ms debounce)\n",
                g_config.button_pins[0],
                g_config.button_pins[1],
                g_config.button_pins[2],
                g_config.button_pins[3],
                static_cast<unsigned>(g_config.debounce_ms));

  const uint32_t now_ms = millis();
  g_last_sample_ms = now_ms;
  g_last_report_ms = now_ms;
}

void loop() {
  const uint32_t now_ms = millis();

  if (static_cast<uint32_t>(now_ms - g_last_sample_ms) >= kSampleIntervalMs) {
    g_last_sample_ms = now_ms;
    tandem::RawStickInput raw{};
    g_scanner.sample(now_ms, raw);

    for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
      if (g_config.axis_pins[i] >= 0) {
        g_axis_stats[i].record(raw.axes[i]);
      }
    }

    if (static_cast<uint32_t>(now_ms - g_last_report_ms) >= kReportIntervalMs) {
      g_last_report_ms = now_ms;

      if constexpr (tandem::board::kPinStatusLed >= 0) {
        g_led_state = !g_led_state;
        digitalWrite(tandem::board::kPinStatusLed, g_led_state ? HIGH : LOW);
      }

      Serial.printf("[%lu ms] ", static_cast<unsigned long>(now_ms));
      for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
        if (g_config.axis_pins[i] >= 0) {
          Serial.printf("A%u=%ld(pp=%ld) ",
                        static_cast<unsigned>(i),
                        static_cast<long>(g_axis_stats[i].last_val),
                        static_cast<long>(g_axis_stats[i].peak_to_peak()));
          g_axis_stats[i].reset();
        }
      }
      Serial.printf("| Btns=0x%08lX | Hat=%u\n",
                    static_cast<unsigned long>(raw.buttons),
                    static_cast<unsigned>(raw.hat));
    }
  }
}

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "boards/board_config.h"
#include "tandem/axis_shaping.h"
#include "tandem/espnow_packet.h"
#include "tandem/input_pipeline.h"
#include "tandem/synthetic_input.h"
#include "tandem/types.h"
#include "tandem_hal/input_scanner.h"

static_assert(tandem::board::kHasEspNow, "espnow_tx requires a board with ESP-NOW support");

#ifndef TANDEM_ESPNOW_STICK_ID
#define TANDEM_ESPNOW_STICK_ID 1
#endif

#ifndef TANDEM_ESPNOW_CHANNEL
#define TANDEM_ESPNOW_CHANNEL 1
#endif

#ifndef TANDEM_ESPNOW_TX_HZ
#define TANDEM_ESPNOW_TX_HZ 250
#endif

#ifndef TANDEM_ESPNOW_PEER_MAC
#define TANDEM_ESPNOW_PEER_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#endif

namespace {

constexpr tandem::StickId kStickId =
    static_cast<tandem::StickId>(TANDEM_ESPNOW_STICK_ID);
static_assert(kStickId == tandem::StickId::Left || kStickId == tandem::StickId::Right,
              "TANDEM_ESPNOW_STICK_ID must be 1 (Left) or 2 (Right)");

constexpr uint8_t kEspNowChannel = static_cast<uint8_t>(TANDEM_ESPNOW_CHANNEL);
constexpr uint32_t kTxIntervalUs = 1000000UL / static_cast<uint32_t>(TANDEM_ESPNOW_TX_HZ);
constexpr uint32_t kStatusIntervalMs = 2000;
constexpr uint32_t kLedSynthIntervalMs = 250;
constexpr uint32_t kLedLiveIntervalMs = 1000;
constexpr uint8_t kBootButtonPin = 0;  // ESP32 BOOT/GPIO0 (active-low)
constexpr uint32_t kButtonDebounceMs = 30;

constexpr uint8_t kPeerMac[6] = TANDEM_ESPNOW_PEER_MAC;

std::atomic<uint32_t> g_ack_ok{0};
std::atomic<uint32_t> g_ack_fail{0};

void on_espnow_send(const esp_now_send_info_t*, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    g_ack_ok.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_ack_fail.fetch_add(1, std::memory_order_relaxed);
  }
}

tandem::hal::InputScanner g_scanner;
tandem::hal::InputScannerConfig g_scanner_config{};
tandem::StickInputConfig g_input_config{};

#if defined(TANDEM_ESPNOW_SYNTHETIC_DEFAULT)
bool g_synthetic_enabled = true;
#else
bool g_synthetic_enabled = false;
#endif

uint32_t g_last_tx_us = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_led_ms = 0;
bool g_led_state = false;

uint16_t g_seq = 0;
uint32_t g_tx_queued = 0;
uint32_t g_tx_errors = 0;
tandem::RawStickInput g_last_raw{};
tandem::StickState g_last_state = tandem::make_centered_state(tandem::kStateFlagConnected);

bool g_boot_stable_pressed = false;
bool g_boot_last_raw_pressed = false;
uint32_t g_boot_last_change_ms = 0;

bool setup_espnow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    return false;
  }
  if (esp_now_register_send_cb(on_espnow_send) != ESP_OK) {
    return false;
  }

  esp_now_peer_info_t peer{};
  std::memcpy(peer.peer_addr, kPeerMac, sizeof(peer.peer_addr));
  peer.channel = kEspNowChannel;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    return false;
  }
  return true;
}

void poll_toggle_inputs(uint32_t now_ms) {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == 't' || ch == 'T') {
      g_synthetic_enabled = !g_synthetic_enabled;
      Serial.printf("[espnow_tx] mode -> %s\n",
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
      Serial.printf("[espnow_tx] BOOT button -> mode %s\n",
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

  Serial.begin(115200);
  delay(200);

  const bool espnow_ok = setup_espnow();
  uint8_t local_mac[6]{};
  esp_wifi_get_mac(WIFI_IF_STA, local_mac);

  Serial.println();
  Serial.println("=== T.A.N.D.E.M. ESP-NOW Stick TX (Mode B) ===");
  Serial.printf("Board      : %s\n", tandem::board::kBoardName);
  Serial.printf("Stick ID   : %s (%u)\n",
                (kStickId == tandem::StickId::Left) ? "LEFT" : "RIGHT",
                static_cast<unsigned>(kStickId));
  Serial.printf("Local MAC  : %02X:%02X:%02X:%02X:%02X:%02X\n",
                local_mac[0], local_mac[1], local_mac[2],
                local_mac[3], local_mac[4], local_mac[5]);
  Serial.printf("Peer MAC   : %02X:%02X:%02X:%02X:%02X:%02X (ch=%u, %lu Hz)\n",
                kPeerMac[0], kPeerMac[1], kPeerMac[2],
                kPeerMac[3], kPeerMac[4], kPeerMac[5],
                static_cast<unsigned>(kEspNowChannel),
                static_cast<unsigned long>(TANDEM_ESPNOW_TX_HZ));
  Serial.printf("ESP-NOW    : %s\n", espnow_ok ? "READY" : "INIT FAILED");
  Serial.printf("Mode       : %s (press BOOT/GPIO0 or send 't' to toggle)\n",
                g_synthetic_enabled ? "SYNTHETIC" : "LIVE");

  const uint32_t now_ms = millis();
  g_last_tx_us = micros();
  g_last_status_ms = now_ms;
  g_last_led_ms = now_ms;
}

void loop() {
  const uint32_t now_us = micros();
  const uint32_t now_ms = millis();

  poll_toggle_inputs(now_ms);

  if (static_cast<uint32_t>(now_us - g_last_tx_us) >= kTxIntervalUs) {
    g_last_tx_us = now_us;

    if (g_synthetic_enabled) {
      g_last_state = tandem::make_synthetic_stick_state(now_ms, kStickId);
    } else {
      g_scanner.sample(now_ms, g_last_raw);
      g_last_state = tandem::process_stick_input(
          g_last_raw, g_input_config, tandem::kStateFlagConnected);
    }

    tandem::EspNowPacketBuffer pkt{};
    if (tandem::encode_espnow_packet(
            kStickId, g_seq, g_last_state, pkt.bytes, sizeof(pkt.bytes))) {
      ++g_seq;
      if (esp_now_send(kPeerMac, pkt.bytes, sizeof(pkt.bytes)) == ESP_OK) {
        ++g_tx_queued;
      } else {
        ++g_tx_errors;
      }
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
        "[%lu ms] stick=%c mode=%s tx=%lu err=%lu ack_ok=%lu ack_fail=%lu | raw=[%ld,%ld,%ld,%ld] out=[%d,%d,%d,%d] btns=0x%08lX hat=%u\n",
        static_cast<unsigned long>(now_ms),
        (kStickId == tandem::StickId::Left) ? 'L' : 'R',
        g_synthetic_enabled ? "SYNTH" : "LIVE",
        static_cast<unsigned long>(g_tx_queued),
        static_cast<unsigned long>(g_tx_errors),
        static_cast<unsigned long>(g_ack_ok.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(g_ack_fail.load(std::memory_order_relaxed)),
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

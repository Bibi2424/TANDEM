#include "transport_espnow_tx.h"

#if TANDEM_CAP_ESPNOW

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "tandem/espnow_packet.h"

namespace tandem::stick {

namespace {

std::atomic<uint32_t> g_ack_ok{0};
std::atomic<uint32_t> g_ack_fail{0};

void on_espnow_send(const esp_now_send_info_t*, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    g_ack_ok.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_ack_fail.fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace

void EspNowTxTransport::configure(uint8_t channel, const uint8_t peer_mac[6]) noexcept {
  channel_ = channel;
  std::memcpy(peer_mac_, peer_mac, sizeof(peer_mac_));
}

bool EspNowTxTransport::start() noexcept {
  if (active_) {
    return true;
  }

  if (!WiFi.mode(WIFI_STA)) {
    return false;
  }
  WiFi.disconnect(false, true);
  esp_wifi_set_ps(WIFI_PS_NONE);
  if (esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    WiFi.mode(WIFI_OFF);
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  if (esp_now_register_send_cb(on_espnow_send) != ESP_OK) {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    return false;
  }

  esp_now_peer_info_t peer{};
  std::memcpy(peer.peer_addr, peer_mac_, sizeof(peer.peer_addr));
  peer.channel = channel_;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    return false;
  }

  active_ = true;
  return true;
}

void EspNowTxTransport::stop() noexcept {
  if (!active_) {
    return;
  }
  esp_now_deinit();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  active_ = false;
}

bool EspNowTxTransport::send(tandem::StickId stick_id, const tandem::StickState& state) noexcept {
  if (!active_) {
    return false;
  }
  tandem::EspNowPacketBuffer pkt{};
  if (!tandem::encode_espnow_packet(stick_id, seq_, state, pkt.bytes, sizeof(pkt.bytes))) {
    return false;
  }
  ++seq_;
  if (esp_now_send(peer_mac_, pkt.bytes, sizeof(pkt.bytes)) == ESP_OK) {
    ++tx_queued_;
    return true;
  }
  ++tx_errors_;
  return false;
}

void EspNowTxTransport::fill_telemetry(tandem::config::TelemetrySnapshot& out) const noexcept {
  out.has_dropped = true;
  out.dropped_packets = tx_errors_ + g_ack_fail.load(std::memory_order_relaxed);
  out.has_peer_status = true;
  bool is_broadcast = true;
  for (uint8_t b : peer_mac_) {
    if (b != 0xFFU) {
      is_broadcast = false;
      break;
    }
  }
  out.peer_status = is_broadcast ? "BROADCAST" : "UNICAST";
}

void EspNowTxTransport::format_stats(char* out, size_t out_len) const noexcept {
  std::snprintf(out, out_len, "tx=%lu err=%lu ack_ok=%lu ack_fail=%lu", static_cast<unsigned long>(tx_queued_),
                static_cast<unsigned long>(tx_errors_), static_cast<unsigned long>(g_ack_ok.load(std::memory_order_relaxed)),
                static_cast<unsigned long>(g_ack_fail.load(std::memory_order_relaxed)));
}

}  // namespace tandem::stick

#endif  // TANDEM_CAP_ESPNOW

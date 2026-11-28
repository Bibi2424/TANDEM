// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/config_protocol.h"
#include "tandem/types.h"

namespace tandem::stick {

class StickTransport {
 public:
  virtual ~StickTransport() = default;

  virtual uint32_t interval_us() const noexcept = 0;
  virtual bool is_active() const noexcept = 0;
  virtual bool start() noexcept = 0;
  virtual void stop() noexcept = 0;
  virtual void poll() noexcept {}
  virtual void on_stick_id_changed() noexcept {}
  virtual const char* device_name() const noexcept { return nullptr; }
  virtual void fill_telemetry(tandem::config::TelemetrySnapshot&) const noexcept {}
  virtual bool send(tandem::StickId stick_id, const tandem::StickState& state) noexcept = 0;
  virtual void format_stats(char* out, size_t out_len) const noexcept = 0;
};

}  // namespace tandem::stick

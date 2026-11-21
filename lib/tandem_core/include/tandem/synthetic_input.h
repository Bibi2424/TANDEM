#pragma once

#include <cstdint>

#include "tandem/types.h"

namespace tandem {

/// Generates a deterministic, hardware-free StickState test pattern for bench/transport
/// bring-up without physical potentiometers or buttons wired.
/// - 8 axes sweep smoothly over [-kAxisMax, kAxisMax] with a 4-second period and per-axis phase.
/// - 32 buttons cycle one bit at a time every 250 ms.
/// - 8-way POV hat steps through 0 (centered) and 1..8 every 500 ms.
/// - StickId::Right shifts axis phases (+90 deg) and button offset (+16) so dual-stick
///   transports remain visually distinguishable.
StickState make_synthetic_stick_state(uint32_t now_ms,
                                      StickId stick_id = StickId::Left) noexcept;

}  // namespace tandem

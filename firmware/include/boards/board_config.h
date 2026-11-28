// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#if defined(TANDEM_BOARD_ESP32_WROOM)
#include "boards/esp32_wroom.h"
#elif defined(TANDEM_BOARD_ESP32_S2)
#include "boards/esp32_s2.h"
#elif defined(TANDEM_BOARD_ESP32_S3)
#include "boards/esp32_s3.h"
#else
#error "No T.A.N.D.E.M. board selected. Define one of TANDEM_BOARD_ESP32_WROOM, TANDEM_BOARD_ESP32_S2, or TANDEM_BOARD_ESP32_S3."
#endif

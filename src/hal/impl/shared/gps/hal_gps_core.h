#pragma once

/**
 * @file hal_gps_core.h
 * @brief Internal seam between the hardware GPS backends and the shared
 *        NMEA-parsing engine (gps_nmea_parser).
 *
 * The engine (encode + all hal_gps_* getters) lives in hal_gps_core.cpp and is
 * shared by every hardware backend. Each backend implements only the serial
 * transport (hal_gps_init / hal_gps_update / hal_gps_serial_available) and uses
 * these helpers to feed bytes and drive (re)initialisation.
 */

#include "../../../hal_target.h"
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474
#include "../../../hal_config.h"
#ifdef HAL_ENABLE_GPS

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Reset the parser and all decoded state (used by backends on init). */
void hal_gps_engine_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_GPS */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */

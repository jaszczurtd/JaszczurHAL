#pragma once

/**
 * @file hal_gps_core.h
 * @brief Internal control and mock-injection API for the shared GPS engine.
 *
 * The engine owns hal_gps_encode() and all transport-independent public
 * getters. The portable facade in hal/gps/hal_gps.cpp owns compile-time
 * transport selection, initialization, polling, and serial availability.
 */

#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"
#ifdef HAL_ENABLE_GPS

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Reset the parser and all decoded state (used by backends on init). */
void hal_gps_engine_reset(void);

#if HAL_TARGET_IS_MOCK
void hal_gps_engine_mock_set_location(double lat, double lng);
void hal_gps_engine_mock_set_valid(bool valid);
void hal_gps_engine_mock_set_updated(bool updated);
void hal_gps_engine_mock_set_age(uint32_t age_ms);
void hal_gps_engine_mock_set_speed(double kmph);
void hal_gps_engine_mock_set_date(int year, int month, int day);
void hal_gps_engine_mock_set_time(int hour, int minute, int second);
void hal_gps_engine_mock_set_altitude(double altitude_m);
void hal_gps_engine_mock_set_course(double course_deg);
void hal_gps_engine_mock_set_dop(double hdop, double vdop, double pdop);
void hal_gps_engine_mock_set_satellites(uint32_t used, uint8_t in_view);
void hal_gps_engine_mock_set_fix(uint8_t quality, uint8_t mode);
void hal_gps_engine_mock_set_horizontal_accuracy(double accuracy_m);
void hal_gps_engine_mock_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_GPS */

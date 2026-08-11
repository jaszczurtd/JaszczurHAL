#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_GPS

#include "hal/gps/hal_gps_core.h"

void hal_mock_gps_set_location(double lat, double lng) {
  hal_gps_engine_mock_set_location(lat, lng);
}

void hal_mock_gps_set_valid(bool valid) {
  hal_gps_engine_mock_set_valid(valid);
}

void hal_mock_gps_set_updated(bool updated) {
  hal_gps_engine_mock_set_updated(updated);
}

void hal_mock_gps_set_age(uint32_t age_ms) {
  hal_gps_engine_mock_set_age(age_ms);
}

void hal_mock_gps_set_speed(double kmph) {
  hal_gps_engine_mock_set_speed(kmph);
}

void hal_mock_gps_set_date(int year, int month, int day) {
  hal_gps_engine_mock_set_date(year, month, day);
}

void hal_mock_gps_set_time(int hour, int minute, int second) {
  hal_gps_engine_mock_set_time(hour, minute, second);
}

void hal_mock_gps_set_altitude_m(double altitude_m) {
  hal_gps_engine_mock_set_altitude(altitude_m);
}

void hal_mock_gps_set_course_deg(double course_deg) {
  hal_gps_engine_mock_set_course(course_deg);
}

void hal_mock_gps_set_dop(double hdop, double vdop, double pdop) {
  hal_gps_engine_mock_set_dop(hdop, vdop, pdop);
}

void hal_mock_gps_set_satellites(uint32_t used, uint8_t in_view) {
  hal_gps_engine_mock_set_satellites(used, in_view);
}

void hal_mock_gps_set_fix(uint8_t quality, uint8_t mode) {
  hal_gps_engine_mock_set_fix(quality, mode);
}

void hal_mock_gps_set_horizontal_accuracy_m(double accuracy_m) {
  hal_gps_engine_mock_set_horizontal_accuracy(accuracy_m);
}

void hal_mock_gps_reset(void) { hal_gps_engine_mock_reset(); }

#endif /* HAL_ENABLE_GPS */
#endif /* HAL_TARGET_IS_MOCK */

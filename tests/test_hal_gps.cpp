#include "hal/hal_gps.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void setUp(void) { hal_gps_init(4, 5, 9600, HAL_UART_CFG_8N1); }

void tearDown(void) {}

static void feed_nmea(const char *body) {
  uint8_t checksum = 0u;
  hal_gps_encode('$');
  for (const char *c = body; *c != '\0'; ++c) {
    checksum ^= (uint8_t)*c;
    hal_gps_encode(*c);
  }

  char tail[8] = {};
  (void)snprintf(tail, sizeof(tail), "*%02X\r\n", checksum);
  for (const char *c = tail; *c != '\0'; ++c) {
    hal_gps_encode(*c);
  }
}

void test_location_flags_and_age(void) {
  hal_mock_gps_set_valid(true);
  hal_mock_gps_set_updated(true);
  hal_mock_gps_set_age(3210);

  TEST_ASSERT_TRUE(hal_gps_location_is_valid());
  TEST_ASSERT_TRUE(hal_gps_location_is_updated());
  TEST_ASSERT_EQUAL_UINT32(3210, hal_gps_location_age());
}

void test_location_speed_date_time_getters(void) {
  hal_mock_gps_set_location(52.2297, 21.0122);
  hal_mock_gps_set_speed(87.5);
  hal_mock_gps_set_date(2026, 3, 31);
  hal_mock_gps_set_time(14, 45, 12);

  TEST_ASSERT_TRUE(fabs(hal_gps_latitude() - 52.2297) < 0.0001);
  TEST_ASSERT_TRUE(fabs(hal_gps_longitude() - 21.0122) < 0.0001);
  TEST_ASSERT_TRUE(fabs(hal_gps_speed_kmph() - 87.5) < 0.0001);
  TEST_ASSERT_EQUAL_INT(2026, hal_gps_date_year());
  TEST_ASSERT_EQUAL_INT(3, hal_gps_date_month());
  TEST_ASSERT_EQUAL_INT(31, hal_gps_date_day());
  TEST_ASSERT_EQUAL_INT(14, hal_gps_time_hour());
  TEST_ASSERT_EQUAL_INT(45, hal_gps_time_minute());
  TEST_ASSERT_EQUAL_INT(12, hal_gps_time_second());
}

void test_init_resets_mock_state(void) {
  hal_mock_gps_set_valid(true);
  hal_mock_gps_set_location(1.0, 2.0);

  hal_gps_init(6, 7, 9600, HAL_UART_CFG_7N1);

  TEST_ASSERT_FALSE(hal_gps_location_is_valid());
  TEST_ASSERT_TRUE(fabs(hal_gps_latitude() - 0.0) < 0.0001);
  TEST_ASSERT_TRUE(fabs(hal_gps_longitude() - 0.0) < 0.0001);
}

void test_update_and_encode_do_not_corrupt_state_in_mock(void) {
  hal_mock_gps_set_location(50.0, 19.0);
  hal_mock_gps_set_valid(true);

  hal_gps_update();
  hal_gps_encode('$');

  TEST_ASSERT_TRUE(hal_gps_location_is_valid());
  TEST_ASSERT_TRUE(fabs(hal_gps_latitude() - 50.0) < 0.0001);
  TEST_ASSERT_TRUE(fabs(hal_gps_longitude() - 19.0) < 0.0001);
}

void test_public_encode_uses_shared_nmea_engine(void) {
  feed_nmea("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,100525,003.1,W");

  TEST_ASSERT_TRUE(hal_gps_location_is_valid());
  TEST_ASSERT_TRUE(hal_gps_location_is_updated());
  TEST_ASSERT_TRUE(fabs(hal_gps_latitude() - 48.1173) < 0.0005);
  TEST_ASSERT_FALSE(hal_gps_location_is_updated());
  TEST_ASSERT_TRUE(fabs(hal_gps_longitude() - 11.516666) < 0.0005);
  TEST_ASSERT_TRUE(fabs(hal_gps_speed_kmph() - 41.48) < 0.05);
  TEST_ASSERT_EQUAL_INT(2025, hal_gps_date_year());
  TEST_ASSERT_EQUAL_INT(5, hal_gps_date_month());
  TEST_ASSERT_EQUAL_INT(10, hal_gps_date_day());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_gps_passed_checksum());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_gps_sentences_with_fix());
}

void test_extended_fix_fields(void) {
  hal_mock_gps_set_altitude_m(123.4);
  hal_mock_gps_set_course_deg(270.5);
  hal_mock_gps_set_dop(0.9, 1.5, 2.1);
  hal_mock_gps_set_satellites(9, 14);
  hal_mock_gps_set_fix(2, 3);
  hal_mock_gps_set_horizontal_accuracy_m(2.5);

  TEST_ASSERT_TRUE(fabs(hal_gps_altitude_m() - 123.4) < 0.001);
  TEST_ASSERT_TRUE(fabs(hal_gps_course_deg() - 270.5) < 0.001);
  TEST_ASSERT_TRUE(fabs(hal_gps_hdop() - 0.9) < 0.001);
  TEST_ASSERT_TRUE(fabs(hal_gps_vdop() - 1.5) < 0.001);
  TEST_ASSERT_TRUE(fabs(hal_gps_pdop() - 2.1) < 0.001);
  TEST_ASSERT_EQUAL_UINT32(9, hal_gps_satellites_used());
  TEST_ASSERT_EQUAL_UINT8(14, hal_gps_satellites_in_view());
  TEST_ASSERT_EQUAL_UINT8(2, hal_gps_fix_quality());
  TEST_ASSERT_EQUAL_UINT8(3, hal_gps_fix_mode());
  TEST_ASSERT_TRUE(fabs(hal_gps_horizontal_accuracy_m() - 2.5) < 0.001);
}

void test_mock_reset_clears_injected_state(void) {
  hal_mock_gps_set_location(12.5, -7.25);
  hal_mock_gps_set_valid(true);
  hal_mock_gps_set_speed(42.0);

  hal_mock_gps_reset();

  TEST_ASSERT_FALSE(hal_gps_location_is_valid());
  TEST_ASSERT_TRUE(fabs(hal_gps_latitude()) < 0.0001);
  TEST_ASSERT_TRUE(fabs(hal_gps_longitude()) < 0.0001);
  TEST_ASSERT_TRUE(fabs(hal_gps_speed_kmph()) < 0.0001);
  TEST_ASSERT_EQUAL_INT(0, hal_gps_date_year());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_gps_chars_processed());
}

void test_diagnostics_default_to_zero_in_mock(void) {
  TEST_ASSERT_EQUAL_UINT32(0, hal_gps_chars_processed());
  TEST_ASSERT_EQUAL_UINT32(0, hal_gps_passed_checksum());
  TEST_ASSERT_EQUAL_UINT32(0, hal_gps_failed_checksum());
  TEST_ASSERT_EQUAL_UINT32(0, hal_gps_sentences_with_fix());
  TEST_ASSERT_EQUAL_INT(0, hal_gps_serial_available());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_location_flags_and_age);
  RUN_TEST(test_location_speed_date_time_getters);
  RUN_TEST(test_init_resets_mock_state);
  RUN_TEST(test_update_and_encode_do_not_corrupt_state_in_mock);
  RUN_TEST(test_public_encode_uses_shared_nmea_engine);
  RUN_TEST(test_extended_fix_fields);
  RUN_TEST(test_mock_reset_clears_injected_state);
  RUN_TEST(test_diagnostics_default_to_zero_in_mock);
  return UNITY_END();
}

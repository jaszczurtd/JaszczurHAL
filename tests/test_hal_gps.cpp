#include "hal/gps/hal_gps.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_RMC_FIX                                                           \
  "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,100525,003.1,W"

void setUp(void) { hal_gps_init(4, 5, 9600, HAL_UART_CFG_8N1); }

void tearDown(void) { hal_mock_gps_engine_full_reset(); }

/* Feed @p len bytes of a sentence body with a valid checksum; the length lets
 * a body carry NUL bytes. */
static void feed_nmea_n(const char *body, size_t len) {
  uint8_t checksum = 0u;
  hal_gps_encode('$');
  for (size_t i = 0u; i < len; ++i) {
    checksum ^= (uint8_t)body[i];
    hal_gps_encode(body[i]);
  }

  char tail[8] = {};
  (void)snprintf(tail, sizeof(tail), "*%02X\r\n", checksum);
  for (const char *c = tail; *c != '\0'; ++c) {
    hal_gps_encode(*c);
  }
}

static void feed_nmea(const char *body) { feed_nmea_n(body, strlen(body)); }

/* String literal body that may contain NUL bytes. */
#define FEED_NMEA_BYTES(literal) feed_nmea_n(literal, sizeof(literal) - 1u)

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

void test_pause_resume_is_idempotent_and_preserves_fix(void) {
  hal_mock_gps_set_location(50.0, 19.0);
  hal_mock_gps_set_valid(true);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_pause());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_pause());
  TEST_ASSERT_TRUE(hal_gps_location_is_valid());
  TEST_ASSERT_TRUE(fabs(hal_gps_latitude() - 50.0) < 0.0001);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_resume());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_resume());
  TEST_ASSERT_TRUE(hal_gps_location_is_valid());
  TEST_ASSERT_TRUE(fabs(hal_gps_longitude() - 19.0) < 0.0001);
}

void test_public_encode_uses_shared_nmea_engine(void) {
  feed_nmea(TEST_RMC_FIX);

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

static void assert_utc_equal(const hal_gps_utc_t *expected,
                             const hal_gps_utc_t *actual) {
  TEST_ASSERT_EQUAL_UINT16(expected->year, actual->year);
  TEST_ASSERT_EQUAL_UINT8(expected->month, actual->month);
  TEST_ASSERT_EQUAL_UINT8(expected->day, actual->day);
  TEST_ASSERT_EQUAL_UINT8(expected->hour, actual->hour);
  TEST_ASSERT_EQUAL_UINT8(expected->minute, actual->minute);
  TEST_ASSERT_EQUAL_UINT8(expected->second, actual->second);
  TEST_ASSERT_EQUAL_UINT32(expected->age_ms, actual->age_ms);
}

void test_get_utc_rejects_null(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_gps_get_utc(NULL));
}

void test_get_utc_before_any_rmc_leaves_output(void) {
  hal_gps_utc_t utc = {};
  utc.year = 1234u;
  feed_nmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");

  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_gps_get_utc(&utc));
  TEST_ASSERT_EQUAL_UINT16(1234u, utc.year);
}

void test_get_utc_reports_rmc_fields_and_age(void) {
  hal_mock_set_millis(1000u);
  feed_nmea(TEST_RMC_FIX);
  hal_mock_set_millis(1750u);

  const hal_gps_utc_t expected = {2025u, 5u, 10u, 12u, 35u, 19u, 750u};
  hal_gps_utc_t utc = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_get_utc(&utc));
  assert_utc_equal(&expected, &utc);
}

void test_utc_age_counts_from_the_last_rmc_with_fix(void) {
  hal_mock_set_millis(UINT32_MAX - 99u);
  feed_nmea(TEST_RMC_FIX);
  /* A location from GGA and an RMC without a fix leave the UTC age alone. */
  hal_mock_set_millis(200u);
  feed_nmea("GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  hal_mock_set_millis(400u);
  feed_nmea("GPRMC,123521,V,,,,,,,100525,,");

  hal_gps_utc_t utc = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_get_utc(&utc));
  TEST_ASSERT_EQUAL_UINT8(19u, utc.second);
  TEST_ASSERT_EQUAL_UINT32(500u, utc.age_ms);
}

void test_malformed_rmc_keeps_previous_utc_and_age(void) {
  hal_mock_set_millis(1000u);
  feed_nmea(TEST_RMC_FIX);
  hal_mock_set_millis(10000u);
  feed_nmea(
      "GPRMC,garbage,A,4807.038,N,01131.000,E,022.4,084.4,100525,003.1,W");
  feed_nmea(
      "GPRMC,123600,A,4807.038,N,01131.000,E,022.4,084.4,100525oops,003.1,W");
  FEED_NMEA_BYTES("GPRMC,123600\0oops,A,4807.038,N,01131.000,E,022.4,084.4,"
                  "100525,003.1,W");
  FEED_NMEA_BYTES("GPRMC,123600,A\0x,4807.038,N,01131.000,E,022.4,084.4,"
                  "100525,003.1,W");
  FEED_NMEA_BYTES("GPRMC,123600,A,4807.038,N,01131.000,E,022.4,084.4,"
                  "100525\0x,003.1,W");
  hal_mock_set_millis(10050u);

  const hal_gps_utc_t expected = {2025u, 5u, 10u, 12u, 35u, 19u, 9050u};
  hal_gps_utc_t utc = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_get_utc(&utc));
  assert_utc_equal(&expected, &utc);
}

void test_mock_utc_injection_and_reset(void) {
  const hal_gps_utc_t injected = {2026u, 10u, 25u, 1u, 59u, 58u, 2500u};
  hal_mock_gps_set_utc(&injected);

  hal_gps_utc_t utc = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_get_utc(&utc));
  assert_utc_equal(&injected, &utc);

  hal_mock_gps_set_utc(NULL);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_gps_get_utc(&utc));

  hal_mock_gps_set_utc(&injected);
  hal_mock_gps_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_gps_get_utc(&utc));

  hal_mock_gps_set_utc(&injected);
  hal_gps_init(4, 5, 9600, HAL_UART_CFG_8N1);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_gps_get_utc(&utc));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_location_flags_and_age);
  RUN_TEST(test_location_speed_date_time_getters);
  RUN_TEST(test_init_resets_mock_state);
  RUN_TEST(test_update_and_encode_do_not_corrupt_state_in_mock);
  RUN_TEST(test_pause_resume_is_idempotent_and_preserves_fix);
  RUN_TEST(test_public_encode_uses_shared_nmea_engine);
  RUN_TEST(test_extended_fix_fields);
  RUN_TEST(test_mock_reset_clears_injected_state);
  RUN_TEST(test_diagnostics_default_to_zero_in_mock);
  RUN_TEST(test_get_utc_rejects_null);
  RUN_TEST(test_get_utc_before_any_rmc_leaves_output);
  RUN_TEST(test_get_utc_reports_rmc_fields_and_age);
  RUN_TEST(test_utc_age_counts_from_the_last_rmc_with_fix);
  RUN_TEST(test_malformed_rmc_keeps_previous_utc_and_age);
  RUN_TEST(test_mock_utc_injection_and_reset);
  return UNITY_END();
}

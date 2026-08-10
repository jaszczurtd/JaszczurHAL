#include "hal/hal_time.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_time_reset();
}

void tearDown(void) {}

void test_timezone_and_ntp_sync_requests_are_recorded(void) {
  TEST_ASSERT_TRUE(hal_time_set_timezone("CET-1CEST,M3.5.0/2,M10.5.0/3"));
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0/2,M10.5.0/3",
                           hal_mock_time_get_timezone());

  TEST_ASSERT_TRUE(hal_time_sync_ntp("pool.ntp.org", "time.nist.gov"));
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", hal_mock_time_get_ntp_primary());
  TEST_ASSERT_EQUAL_STRING("time.nist.gov", hal_mock_time_get_ntp_secondary());
}

void test_sync_check_and_formatting(void) {
  hal_mock_time_set_unix(200000);
  TEST_ASSERT_TRUE(hal_time_is_synced(172800));
  TEST_ASSERT_EQUAL_UINT64(200000, hal_time_unix());

  struct tm tm_local = {};
  tm_local.tm_year = 126; // 2026
  tm_local.tm_mon = 2;    // March
  tm_local.tm_mday = 30;
  tm_local.tm_hour = 12;
  tm_local.tm_min = 34;
  tm_local.tm_sec = 56;
  hal_mock_time_set_local(&tm_local);

  struct tm out = {};
  TEST_ASSERT_TRUE(hal_time_get_local(&out));
  TEST_ASSERT_EQUAL_INT(30, out.tm_mday);
  TEST_ASSERT_EQUAL_INT(34, out.tm_min);

  char buf[32] = {};
  TEST_ASSERT_TRUE(
      hal_time_format_local(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S"));
  TEST_ASSERT_EQUAL_STRING("30/03/2026 12:34:56", buf);
}

void test_invalid_inputs_are_rejected(void) {
  TEST_ASSERT_FALSE(hal_time_set_timezone(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_time_sync_ntp(NULL, NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_time_from_components_epoch_base(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(1970, 1, 1, 0, 0, 0));
}

void test_time_from_components_leap_day(void) {
  // 2024-02-29 12:00:00 UTC
  TEST_ASSERT_EQUAL_UINT32(1709208000u,
                           hal_time_from_components(2024, 2, 29, 12, 0, 0));
}

void test_time_from_components_invalid_values(void) {
  TEST_ASSERT_EQUAL_UINT32(0u,
                           hal_time_from_components(1969, 12, 31, 23, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2024, 2, 30, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2023, 2, 29, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2026, 4, 31, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2024, 13, 1, 0, 0, 0));
}

void test_time_from_components_rejects_uint32_epoch_overflow(void) {
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                           hal_time_from_components(2106, 2, 7, 6, 28, 15));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2106, 2, 7, 6, 28, 16));
}

void test_daylight_saving_interval_uses_last_sundays(void) {
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 3, 30));
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 3, 31));
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 10, 26));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 10, 27));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 1, 15));
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 7, 15));
}

void test_daylight_saving_rejects_invalid_dates(void) {
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(0, 3, 31));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 13, 1));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 4, 31));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2023, 2, 29));
}

void test_cet_cest_adjustment_applies_offsets_and_rollovers(void) {
  int year = 2024;
  int month = 7;
  int day = 15;
  int hour = 23;
  int minute = 42;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2024, year);
  TEST_ASSERT_EQUAL_INT(7, month);
  TEST_ASSERT_EQUAL_INT(16, day);
  TEST_ASSERT_EQUAL_INT(1, hour);
  TEST_ASSERT_EQUAL_INT(42, minute);

  year = 2024;
  month = 2;
  day = 29;
  hour = 23;
  minute = 59;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2024, year);
  TEST_ASSERT_EQUAL_INT(3, month);
  TEST_ASSERT_EQUAL_INT(1, day);
  TEST_ASSERT_EQUAL_INT(0, hour);
  TEST_ASSERT_EQUAL_INT(59, minute);

  year = 2023;
  month = 12;
  day = 31;
  hour = 23;
  minute = 0;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2024, year);
  TEST_ASSERT_EQUAL_INT(1, month);
  TEST_ASSERT_EQUAL_INT(1, day);
  TEST_ASSERT_EQUAL_INT(0, hour);
}

void test_cet_cest_adjustment_leaves_invalid_or_incomplete_input_unchanged(
    void) {
  int year = 2023;
  int month = 2;
  int day = 29;
  int hour = 23;
  int minute = 0;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2023, year);
  TEST_ASSERT_EQUAL_INT(2, month);
  TEST_ASSERT_EQUAL_INT(29, day);
  TEST_ASSERT_EQUAL_INT(23, hour);

  hal_time_adjust_cet_cest(nullptr, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2, month);
  TEST_ASSERT_EQUAL_INT(29, day);
  TEST_ASSERT_EQUAL_INT(23, hour);
}

void test_half_open_time_range_contract(void) {
  TEST_ASSERT_TRUE(hal_time_is_in_range(0, 0, 10));
  TEST_ASSERT_TRUE(hal_time_is_in_range(9, 0, 10));
  TEST_ASSERT_FALSE(hal_time_is_in_range(10, 0, 10));
  TEST_ASSERT_FALSE(hal_time_is_in_range(-1, 0, 10));
  TEST_ASSERT_FALSE(hal_time_is_in_range(5, 10, 0));
}

void test_extract_minutes_handles_sign_and_optional_outputs(void) {
  int hours = 0;
  int minutes = 0;
  hal_time_extract_minutes(125, &hours, &minutes);
  TEST_ASSERT_EQUAL_INT(2, hours);
  TEST_ASSERT_EQUAL_INT(5, minutes);

  hal_time_extract_minutes(-61, &hours, &minutes);
  TEST_ASSERT_EQUAL_INT(-1, hours);
  TEST_ASSERT_EQUAL_INT(-1, minutes);

  minutes = 99;
  hal_time_extract_minutes(60, nullptr, &minutes);
  TEST_ASSERT_EQUAL_INT(0, minutes);
  hal_time_extract_minutes(60, &hours, nullptr);
  TEST_ASSERT_EQUAL_INT(1, hours);
  hal_time_extract_minutes(60, nullptr, nullptr);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_timezone_and_ntp_sync_requests_are_recorded);
  RUN_TEST(test_sync_check_and_formatting);
  RUN_TEST(test_invalid_inputs_are_rejected);
  RUN_TEST(test_time_from_components_epoch_base);
  RUN_TEST(test_time_from_components_leap_day);
  RUN_TEST(test_time_from_components_invalid_values);
  RUN_TEST(test_time_from_components_rejects_uint32_epoch_overflow);
  RUN_TEST(test_daylight_saving_interval_uses_last_sundays);
  RUN_TEST(test_daylight_saving_rejects_invalid_dates);
  RUN_TEST(test_cet_cest_adjustment_applies_offsets_and_rollovers);
  RUN_TEST(
      test_cet_cest_adjustment_leaves_invalid_or_incomplete_input_unchanged);
  RUN_TEST(test_half_open_time_range_contract);
  RUN_TEST(test_extract_minutes_handles_sign_and_optional_outputs);
  return UNITY_END();
}

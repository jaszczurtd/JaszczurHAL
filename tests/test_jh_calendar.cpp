#include "hal/impl/shared/time/jh_calendar.h"
#include "utils/unity.h"

#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static jh_calendar_datetime_t make_datetime(uint16_t year, uint8_t month,
                                            uint8_t day, uint8_t hour = 0u,
                                            uint8_t minute = 0u,
                                            uint8_t second = 0u) {
  const jh_calendar_datetime_t datetime = {
      year, month, day, hour, minute, second, 0u,
  };
  return datetime;
}

void test_leap_year_and_month_lengths_follow_gregorian_rules(void) {
  bool is_leap = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_calendar_is_leap_year(1900u, &is_leap));
  TEST_ASSERT_FALSE(is_leap);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_calendar_is_leap_year(2000u, &is_leap));
  TEST_ASSERT_TRUE(is_leap);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_calendar_is_leap_year(2100u, &is_leap));
  TEST_ASSERT_FALSE(is_leap);

  uint8_t days = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_calendar_days_in_month(2000u, 2u, &days));
  TEST_ASSERT_EQUAL_UINT8(29u, days);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_calendar_days_in_month(2100u, 2u, &days));
  TEST_ASSERT_EQUAL_UINT8(28u, days);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_days_in_month(2026u, 13u, &days));
}

void test_day_of_week_uses_sunday_zero_and_validates_dates(void) {
  uint8_t weekday = UINT8_MAX;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_calendar_day_of_week(1u, 1u, 1u, &weekday));
  TEST_ASSERT_EQUAL_UINT8(1u, weekday); // Monday
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_calendar_day_of_week(2024u, 3u, 31u, &weekday));
  TEST_ASSERT_EQUAL_UINT8(0u, weekday); // Sunday

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_day_of_week(2026u, 4u, 31u, &weekday));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_day_of_week(2026u, 8u, 10u, nullptr));
}

void test_validation_rejects_impossible_dates_and_invalid_ranges(void) {
  jh_calendar_datetime_t datetime = make_datetime(2026u, 4u, 31u);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_validate_datetime(&datetime, 1900u, 2099u));

  datetime = make_datetime(2023u, 2u, 29u);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_validate_datetime(&datetime, 1900u, 2099u));

  datetime = make_datetime(2000u, 2u, 29u, 23u, 59u, 59u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_calendar_validate_datetime(&datetime, 1900u, 2099u));

  datetime.weekday = 7u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_validate_datetime(&datetime, 1900u, 2099u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_validate_datetime(&datetime, 2099u, 1900u));
}

void test_unix_epoch_start_is_a_successful_zero_value(void) {
  const jh_calendar_datetime_t datetime = make_datetime(1970u, 1u, 1u);
  uint64_t epoch = UINT64_MAX;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_calendar_datetime_to_epoch(&datetime, &epoch));
  TEST_ASSERT_EQUAL_UINT64(0u, epoch);

  jh_calendar_datetime_t roundtrip = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_calendar_epoch_to_datetime(epoch, 2099u, &roundtrip));
  TEST_ASSERT_EQUAL_UINT16(1970u, roundtrip.year);
  TEST_ASSERT_EQUAL_UINT8(1u, roundtrip.month);
  TEST_ASSERT_EQUAL_UINT8(1u, roundtrip.day);
  TEST_ASSERT_EQUAL_UINT8(4u, roundtrip.weekday);
}

void test_leap_day_roundtrip_preserves_all_components(void) {
  const jh_calendar_datetime_t datetime =
      make_datetime(2000u, 2u, 29u, 12u, 34u, 56u);
  uint64_t epoch = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_calendar_datetime_to_epoch(&datetime, &epoch));
  TEST_ASSERT_EQUAL_UINT64(951827696u, epoch);

  jh_calendar_datetime_t roundtrip = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_calendar_epoch_to_datetime(epoch, 2099u, &roundtrip));
  TEST_ASSERT_EQUAL_UINT16(datetime.year, roundtrip.year);
  TEST_ASSERT_EQUAL_UINT8(datetime.month, roundtrip.month);
  TEST_ASSERT_EQUAL_UINT8(datetime.day, roundtrip.day);
  TEST_ASSERT_EQUAL_UINT8(datetime.hour, roundtrip.hour);
  TEST_ASSERT_EQUAL_UINT8(datetime.minute, roundtrip.minute);
  TEST_ASSERT_EQUAL_UINT8(datetime.second, roundtrip.second);
  TEST_ASSERT_EQUAL_UINT8(2u, roundtrip.weekday);
}

void test_rtc_upper_boundary_and_epoch_overflow_are_explicit(void) {
  const jh_calendar_datetime_t maximum =
      make_datetime(2099u, 12u, 31u, 23u, 59u, 59u);
  uint64_t epoch = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_calendar_datetime_to_epoch(&maximum, &epoch));
  TEST_ASSERT_EQUAL_UINT64(4102444799u, epoch);

  jh_calendar_datetime_t datetime = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_calendar_epoch_to_datetime(epoch, 2099u, &datetime));
  TEST_ASSERT_EQUAL_UINT16(2099u, datetime.year);
  TEST_ASSERT_EQUAL_UINT8(12u, datetime.month);
  TEST_ASSERT_EQUAL_UINT8(31u, datetime.day);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, jh_calendar_epoch_to_datetime(
                                           epoch + 1u, 2099u, &datetime));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, jh_calendar_epoch_to_datetime(
                                           UINT64_MAX, 2099u, &datetime));
}

void test_pre_unix_date_and_null_outputs_return_status_errors(void) {
  const jh_calendar_datetime_t pre_unix =
      make_datetime(1969u, 12u, 31u, 23u, 59u, 59u);
  uint64_t epoch = 123u;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_calendar_datetime_to_epoch(&pre_unix, &epoch));
  TEST_ASSERT_EQUAL_UINT64(123u, epoch);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_datetime_to_epoch(nullptr, &epoch));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_calendar_epoch_to_datetime(0u, 1969u, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_leap_year_and_month_lengths_follow_gregorian_rules);
  RUN_TEST(test_day_of_week_uses_sunday_zero_and_validates_dates);
  RUN_TEST(test_validation_rejects_impossible_dates_and_invalid_ranges);
  RUN_TEST(test_unix_epoch_start_is_a_successful_zero_value);
  RUN_TEST(test_leap_day_roundtrip_preserves_all_components);
  RUN_TEST(test_rtc_upper_boundary_and_epoch_overflow_are_explicit);
  RUN_TEST(test_pre_unix_date_and_null_outputs_return_status_errors);
  return UNITY_END();
}

#include "utils/unity.h"
#include "hal/hal_time.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

void setUp(void) {
    hal_mock_serial_reset();
    hal_mock_time_reset();
}

void tearDown(void) {}

void test_timezone_and_ntp_sync_requests_are_recorded(void) {
    TEST_ASSERT_TRUE(hal_time_set_timezone("CET-1CEST,M3.5.0/2,M10.5.0/3"));
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0/2,M10.5.0/3", hal_mock_time_get_timezone());

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
    TEST_ASSERT_TRUE(hal_time_format_local(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S"));
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
    TEST_ASSERT_EQUAL_UINT32(1709208000u, hal_time_from_components(2024, 2, 29, 12, 0, 0));
}

void test_time_from_components_invalid_values(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(1969, 12, 31, 23, 59, 59));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2024, 2, 30, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2024, 13, 1, 0, 0, 0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_timezone_and_ntp_sync_requests_are_recorded);
    RUN_TEST(test_sync_check_and_formatting);
    RUN_TEST(test_invalid_inputs_are_rejected);
    RUN_TEST(test_time_from_components_epoch_base);
    RUN_TEST(test_time_from_components_leap_day);
    RUN_TEST(test_time_from_components_invalid_values);
    return UNITY_END();
}

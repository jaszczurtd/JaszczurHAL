#include "utils/unity.h"
#include "hal/hal_gps.h"
#include "hal/impl/.mock/hal_mock.h"
#include <math.h>

void setUp(void) {
    hal_gps_init(4, 5, 9600, HAL_UART_CFG_8N1);
}

void tearDown(void) {}

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
    RUN_TEST(test_diagnostics_default_to_zero_in_mock);
    return UNITY_END();
}

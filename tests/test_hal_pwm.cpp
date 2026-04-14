#include "utils/unity.h"
#include "hal/hal_pwm.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {}
void tearDown(void) {}

void test_default_resolution(void) {
    TEST_ASSERT_EQUAL_UINT8(8, hal_mock_pwm_get_resolution());
}

void test_set_resolution(void) {
    hal_pwm_set_resolution(12);
    TEST_ASSERT_EQUAL_UINT8(12, hal_mock_pwm_get_resolution());
    hal_pwm_set_resolution(8);
}

void test_write_stores_value(void) {
    hal_pwm_write(3, 128);
    TEST_ASSERT_EQUAL_UINT32(128, hal_mock_pwm_get_value(3));
}

void test_write_zero(void) {
    hal_pwm_write(5, 255);
    hal_pwm_write(5, 0);
    TEST_ASSERT_EQUAL_UINT32(0, hal_mock_pwm_get_value(5));
}

void test_write_multiple_pins_independent(void) {
    hal_pwm_write(0, 10);
    hal_pwm_write(1, 20);
    TEST_ASSERT_EQUAL_UINT32(10, hal_mock_pwm_get_value(0));
    TEST_ASSERT_EQUAL_UINT32(20, hal_mock_pwm_get_value(1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_resolution);
    RUN_TEST(test_set_resolution);
    RUN_TEST(test_write_stores_value);
    RUN_TEST(test_write_zero);
    RUN_TEST(test_write_multiple_pins_independent);
    return UNITY_END();
}

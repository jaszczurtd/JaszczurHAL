#include "utils/unity.h"
#include "hal/hal_adc.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {}
void tearDown(void) {}

void test_default_resolution(void) {
    TEST_ASSERT_EQUAL_UINT8(12, hal_mock_adc_get_resolution());
}

void test_set_resolution(void) {
    hal_adc_set_resolution(10);
    TEST_ASSERT_EQUAL_UINT8(10, hal_mock_adc_get_resolution());
    hal_adc_set_resolution(12);
}

void test_inject_and_read(void) {
    hal_mock_adc_inject(2, 1024);
    TEST_ASSERT_EQUAL_INT(1024, hal_adc_read(2));
}

void test_default_value_is_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, hal_adc_read(15));
}

void test_inject_multiple_pins(void) {
    hal_mock_adc_inject(0, 100);
    hal_mock_adc_inject(1, 200);
    TEST_ASSERT_EQUAL_INT(100, hal_adc_read(0));
    TEST_ASSERT_EQUAL_INT(200, hal_adc_read(1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_resolution);
    RUN_TEST(test_set_resolution);
    RUN_TEST(test_inject_and_read);
    RUN_TEST(test_default_value_is_zero);
    RUN_TEST(test_inject_multiple_pins);
    return UNITY_END();
}

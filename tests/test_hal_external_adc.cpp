#include "utils/unity.h"
#include "hal/hal_external_adc.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_ext_adc_init(0x48, 0.1875f);
}

void tearDown(void) {}

void test_init_sets_adc_range(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1875f, hal_mock_ext_adc_get_range());

    hal_ext_adc_init(0x49, 0.1250f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1250f, hal_mock_ext_adc_get_range());

    hal_ext_adc_init_bus(1, 0x48, 0.2500f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2500f, hal_mock_ext_adc_get_range());
}

void test_raw_and_scaled_reads_per_channel(void) {
    hal_mock_ext_adc_inject_raw(0, 1234);
    hal_mock_ext_adc_inject_raw(1, -4321);
    hal_mock_ext_adc_inject_scaled(2, 1.25f);
    hal_mock_ext_adc_inject_scaled(3, -0.75f);

    TEST_ASSERT_EQUAL_INT16(1234, hal_ext_adc_read(0));
    TEST_ASSERT_EQUAL_INT16(-4321, hal_ext_adc_read(1));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.25f, hal_ext_adc_read_scaled(2));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.75f, hal_ext_adc_read_scaled(3));
}

void test_out_of_range_channel_returns_safe_default(void) {
    TEST_ASSERT_EQUAL_INT16(0, hal_ext_adc_read(9));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_ext_adc_read_scaled(9));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_adc_range);
    RUN_TEST(test_raw_and_scaled_reads_per_channel);
    RUN_TEST(test_out_of_range_channel_returns_safe_default);
    return UNITY_END();
}

#include "hal/analog/hal_external_adc.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) { hal_ext_adc_init(0x48, 0.1875f); }

void tearDown(void) {}

void test_init_sets_adc_range(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1875f, hal_mock_ext_adc_get_range());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ext_adc_init(0x49, 0.1250f));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1250f, hal_mock_ext_adc_get_range());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ext_adc_init_bus(1, 0x48, 0.2500f));
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

void test_status_read_variants_report_success_and_invalid_args(void) {
  int16_t raw = 0;
  float scaled = 0.0f;
  hal_mock_ext_adc_inject_raw(0, 111);
  hal_mock_ext_adc_inject_scaled(0, 2.5f);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ext_adc_read_ex(0, &raw));
  TEST_ASSERT_EQUAL_INT16(111, raw);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ext_adc_read_scaled_ex(0, &scaled));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, scaled);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ext_adc_read_ex(4, &raw));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ext_adc_read_ex(0, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ext_adc_read_scaled_ex(4, &scaled));
}

void test_init_rejects_invalid_arguments(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ext_adc_init(0x80u, 0.1875f));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ext_adc_init(0x48u, 0.0f));
}

void test_out_of_range_channel_returns_safe_default(void) {
  TEST_ASSERT_EQUAL_INT16(0, hal_ext_adc_read(9));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_ext_adc_read_scaled(9));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_sets_adc_range);
  RUN_TEST(test_raw_and_scaled_reads_per_channel);
  RUN_TEST(test_status_read_variants_report_success_and_invalid_args);
  RUN_TEST(test_init_rejects_invalid_arguments);
  RUN_TEST(test_out_of_range_channel_returns_safe_default);
  return UNITY_END();
}

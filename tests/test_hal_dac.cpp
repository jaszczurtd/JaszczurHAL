#include "hal/hal_dac.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) {
  hal_mock_dac_reset();
  // Re-init both channels to a known state before each test.
  hal_dac_init(0);
  hal_dac_init(1);
}
void tearDown(void) {}

void test_supported_on_mock(void) { TEST_ASSERT_TRUE(hal_dac_is_supported()); }

void test_resolution_and_max(void) {
  TEST_ASSERT_EQUAL_UINT8(12, hal_dac_resolution_bits());
  TEST_ASSERT_EQUAL_UINT16(4095, hal_dac_max_value());
}

void test_init_valid_and_invalid_channel(void) {
  hal_mock_dac_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_init_ex(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_init_ex(1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dac_init_ex(2));
  TEST_ASSERT_TRUE(hal_dac_init(0));
  TEST_ASSERT_TRUE(hal_dac_init(1));
  TEST_ASSERT_FALSE(hal_dac_init(2));
  TEST_ASSERT_TRUE(hal_mock_dac_is_initialized(0));
  TEST_ASSERT_FALSE(hal_mock_dac_is_initialized(5));
}

void test_write_ex_reports_invalid_and_uninitialized_channel(void) {
  hal_mock_dac_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_dac_write_ex(0, 1234));
  TEST_ASSERT_EQUAL_UINT16(0, hal_mock_dac_get(0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dac_write_ex(2, 1234));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_init_ex(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_write_ex(0, 1234));
  TEST_ASSERT_EQUAL_UINT16(1234, hal_mock_dac_get(0));
}

void test_write_and_read_back(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_write_ex(0, 1234));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_write_ex(1, 4000));
  TEST_ASSERT_EQUAL_UINT16(1234, hal_mock_dac_get(0));
  TEST_ASSERT_EQUAL_UINT16(4000, hal_mock_dac_get(1));

  hal_dac_write(0, 1234);
  hal_dac_write(1, 4000);
  TEST_ASSERT_EQUAL_UINT16(1234, hal_mock_dac_get(0));
  TEST_ASSERT_EQUAL_UINT16(4000, hal_mock_dac_get(1));
}

void test_write_clamps_to_max(void) {
  hal_dac_write(0, 9999);
  TEST_ASSERT_EQUAL_UINT16(4095, hal_mock_dac_get(0));
}

void test_write_millivolts(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_write_millivolts_ex(
                                    0, 3300)); // full scale (VREF default)
  TEST_ASSERT_EQUAL_UINT16(4095, hal_mock_dac_get(0));
  hal_dac_write_millivolts(0, 0);
  TEST_ASSERT_EQUAL_UINT16(0, hal_mock_dac_get(0));
  hal_dac_write_millivolts(1, 1650); // ~half scale
  TEST_ASSERT_UINT16_WITHIN(2, 2047, hal_mock_dac_get(1));
}

void test_millivolts_above_vref_clamped(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dac_write_millivolts_ex(0, 5000));
  TEST_ASSERT_EQUAL_UINT16(4095, hal_mock_dac_get(0));

  hal_dac_write_millivolts(0, 5000); // above VREF -> full scale
  TEST_ASSERT_EQUAL_UINT16(4095, hal_mock_dac_get(0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_supported_on_mock);
  RUN_TEST(test_resolution_and_max);
  RUN_TEST(test_init_valid_and_invalid_channel);
  RUN_TEST(test_write_ex_reports_invalid_and_uninitialized_channel);
  RUN_TEST(test_write_and_read_back);
  RUN_TEST(test_write_clamps_to_max);
  RUN_TEST(test_write_millivolts);
  RUN_TEST(test_millivolts_above_vref_clamped);
  return UNITY_END();
}

#include "hal/analog/hal_pcnt.h"
#include "hal/analog/hal_pcnt_common.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) {
  hal_mock_pcnt_reset();
  hal_pcnt_init(0, 10, HAL_PCNT_EDGE_RISING);
  hal_pcnt_init(1, 11, HAL_PCNT_EDGE_BOTH);
}
void tearDown(void) {}

void test_common_edge_validator_accepts_only_supported_edges(void) {
  TEST_ASSERT_TRUE(jh_hal_pcnt_edge_valid(HAL_PCNT_EDGE_RISING));
  TEST_ASSERT_TRUE(jh_hal_pcnt_edge_valid(HAL_PCNT_EDGE_FALLING));
  TEST_ASSERT_TRUE(jh_hal_pcnt_edge_valid(HAL_PCNT_EDGE_BOTH));
  TEST_ASSERT_FALSE(jh_hal_pcnt_edge_valid((hal_pcnt_edge_t)-1));
  TEST_ASSERT_FALSE(jh_hal_pcnt_edge_valid((hal_pcnt_edge_t)99));
}

void test_supported_and_channel_count(void) {
  TEST_ASSERT_TRUE(hal_pcnt_is_supported());
  TEST_ASSERT_EQUAL_UINT8(4, hal_pcnt_channel_count());
}

void test_init_valid_and_invalid(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pcnt_init_ex(0, 5, HAL_PCNT_EDGE_RISING));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pcnt_init_ex(99, 5, HAL_PCNT_EDGE_RISING));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pcnt_init_ex(0, 5, (hal_pcnt_edge_t)99));
  TEST_ASSERT_TRUE(hal_pcnt_init(0, 5, HAL_PCNT_EDGE_RISING));
  TEST_ASSERT_FALSE(hal_pcnt_init(99, 5, HAL_PCNT_EDGE_RISING));
}

void test_init_records_pin_and_edge(void) {
  hal_pcnt_init(0, 7, HAL_PCNT_EDGE_FALLING);
  TEST_ASSERT_EQUAL_UINT8(7, hal_mock_pcnt_get_pin(0));
  TEST_ASSERT_EQUAL_INT(HAL_PCNT_EDGE_FALLING, hal_mock_pcnt_get_edge(0));
}

void test_inject_and_read(void) {
  hal_mock_pcnt_inject(0, 123);
  hal_mock_pcnt_inject(0, 7);
  uint32_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pcnt_read_ex(0, &count));
  TEST_ASSERT_EQUAL_UINT32(130, count);
  TEST_ASSERT_EQUAL_UINT32(130, hal_pcnt_read(0));
}

void test_reset(void) {
  hal_mock_pcnt_inject(1, 50);
  TEST_ASSERT_EQUAL_UINT32(50, hal_pcnt_read(1));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pcnt_reset(1));
  TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(1));
  hal_mock_pcnt_inject(1, 50);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pcnt_reset(1));
  TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(1));
}

void test_read_and_reset(void) {
  hal_mock_pcnt_inject(0, 42);
  uint32_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pcnt_read_and_reset_ex(0, &count));
  TEST_ASSERT_EQUAL_UINT32(42, count);
  TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(0));
  hal_mock_pcnt_inject(0, 42);
  TEST_ASSERT_EQUAL_UINT32(42, hal_pcnt_read_and_reset(0));
  TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(0));
}

void test_uninitialised_channel_reads_zero(void) {
  uint32_t count = 123u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_pcnt_read_ex(3, &count));
  TEST_ASSERT_EQUAL_UINT32(0, count);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_pcnt_reset(3));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_pcnt_read_and_reset_ex(3, &count));
  TEST_ASSERT_EQUAL_UINT32(0, hal_pcnt_read(3)); // valid but not init in setUp
}

void test_status_api_reports_invalid_arguments(void) {
  uint32_t count = 123u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pcnt_read_ex(99, &count));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pcnt_read_ex(0, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pcnt_reset(99));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pcnt_read_and_reset_ex(99, &count));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_pcnt_read_and_reset_ex(0, NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_common_edge_validator_accepts_only_supported_edges);
  RUN_TEST(test_supported_and_channel_count);
  RUN_TEST(test_init_valid_and_invalid);
  RUN_TEST(test_init_records_pin_and_edge);
  RUN_TEST(test_inject_and_read);
  RUN_TEST(test_reset);
  RUN_TEST(test_read_and_reset);
  RUN_TEST(test_uninitialised_channel_reads_zero);
  RUN_TEST(test_status_api_reports_invalid_arguments);
  return UNITY_END();
}

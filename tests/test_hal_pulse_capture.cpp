#include "hal/analog/hal_pulse_capture.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"
#include <string.h>

static const hal_pulse_capture_config_t config = {2, true, 10000};
void setUp(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
  hal_mock_set_micros(1000);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_init(&config));
}
void tearDown(void) { TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit()); }

static void inject(uint32_t first, unsigned count, uint32_t time_us = 1000) {
  for (unsigned i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_mock_pulse_capture_edge(first + i * 500U,
                                                          time_us + i * 31U));
  }
  hal_mock_set_micros(time_us + count * 31U);
}

static void full_periods_and_delayed_consumer(void) {
  inject(900, 32);
  hal_pulse_capture_sample_t result;
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL(HAL_OK, hal_mock_pulse_capture_edge(16900, 1992));
  hal_mock_set_micros(6000); /* Processing latency must not change frequency. */
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL_UINT32(16000, result.ticks);
  TEST_ASSERT_EQUAL_UINT32(16000000, result.clock_hz);
  TEST_ASSERT_EQUAL_UINT32(1992, result.measured_us);
  TEST_ASSERT_EQUAL_UINT16(32, result.periods);
  TEST_ASSERT_EQUAL_UINT32(1, result.sequence);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_pulse_capture_read(&result));
}

static void counter_and_wall_clock_wrap(void) {
  hal_mock_set_micros(UINT32_MAX - 2000U);
  hal_pulse_capture_deinit();
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_init(&config));
  inject(UINT32_MAX - 5000U, 65, UINT32_MAX - 1000U);
  hal_pulse_capture_sample_t result;
  for (unsigned i = 1; i <= 2; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_read(&result));
    TEST_ASSERT_EQUAL_UINT32(16000, result.ticks);
    TEST_ASSERT_EQUAL_UINT32(i, result.sequence);
  }
}

static void timeout_discards_partial_interval(void) {
  inject(0, 16);
  hal_pulse_capture_sample_t result;
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_pulse_capture_read(&result));
  hal_mock_set_micros(20000);
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, hal_pulse_capture_read(&result));
  inject(320000, 32, 20000);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL(HAL_OK, hal_mock_pulse_capture_edge(336000, 20992));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL_UINT32(16000, result.ticks);
}

static void gap_without_empty_read_discards_interval(void) {
  inject(0, 16);
  hal_pulse_capture_sample_t result;
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_pulse_capture_read(&result));
  inject(400000, 34, 25000);
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL_UINT32(16000, result.ticks);
}

static void overflow_is_sticky_and_output_unchanged(void) {
  hal_pulse_capture_sample_t before, result;
  memset(&before, 0x5a, sizeof(before));
  result = before;
  for (unsigned i = 0; i < 1025; ++i)
    hal_mock_pulse_capture_edge(i, 1000);
  TEST_ASSERT_EQUAL(HAL_EOVERFLOW, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL_MEMORY(&before, &result, sizeof(result));
  hal_mock_pulse_capture_fault(HAL_OK);
  TEST_ASSERT_EQUAL(HAL_EOVERFLOW, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_init(&config));
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_pulse_capture_read(&result));
}

static void validation_and_stale_sample(void) {
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pulse_capture_init(nullptr));
  TEST_ASSERT_EQUAL(HAL_EBUSY, hal_pulse_capture_init(&config));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pulse_capture_read(nullptr));
  inject(0, 33);
  hal_mock_set_micros(20000);
  hal_pulse_capture_sample_t result;
  TEST_ASSERT_EQUAL(HAL_ETIMEOUT, hal_pulse_capture_read(&result));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
  TEST_ASSERT_EQUAL(HAL_EUNINIT, hal_pulse_capture_read(&result));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(full_periods_and_delayed_consumer);
  RUN_TEST(counter_and_wall_clock_wrap);
  RUN_TEST(timeout_discards_partial_interval);
  RUN_TEST(gap_without_empty_read_discards_interval);
  RUN_TEST(overflow_is_sticky_and_output_unchanged);
  RUN_TEST(validation_and_stale_sample);
  return UNITY_END();
}

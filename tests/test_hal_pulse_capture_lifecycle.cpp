#include "hal/analog/jh_pulse_capture_backend.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

namespace {
bool retained;
hal_status_t start_status;
unsigned stop_failures, edges_left, next_edge;
uint16_t source_stride;
const hal_pulse_capture_config_t config = {2, true, 10000};
} // namespace

/* Resource-failure backend: hardware acquisition and cleanup can fail
 * independently. Link this in place of the timestamp-injection backend. */
hal_status_t jh_pulse_capture_start(const hal_pulse_capture_config_t *,
                                    uint32_t *clock_hz, uint16_t *stride) {
  retained = true;
  *clock_hz = 16000000U;
  *stride = source_stride;
  return start_status;
}
hal_status_t jh_pulse_capture_stop(void) {
  if (stop_failures != 0U) {
    --stop_failures;
    return HAL_EHW;
  }
  retained = false;
  return HAL_OK;
}
hal_status_t jh_pulse_capture_next(jh_pulse_capture_edge_t *edge) {
  if (edges_left == 0U)
    return HAL_EAGAIN;
  --edges_left;
  *edge = {900U + next_edge++ * source_stride * 500U, 1000U};
  return HAL_OK;
}

void setUp(void) {
  stop_failures = 0U;
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
  start_status = HAL_OK;
  source_stride = 32U;
  edges_left = next_edge = 0U;
  hal_mock_set_micros(1000U);
}
void tearDown(void) {
  stop_failures = 0U;
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
}

static void failed_start_still_allows_cleanup_retry(void) {
  start_status = HAL_EHW;
  TEST_ASSERT_EQUAL(HAL_EHW, hal_pulse_capture_init(&config));
  TEST_ASSERT_TRUE(retained);
  stop_failures = 1U;
  TEST_ASSERT_EQUAL(HAL_EHW, hal_pulse_capture_deinit());
  TEST_ASSERT_TRUE(retained);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
  TEST_ASSERT_FALSE(retained);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
}

static void failed_stop_preserves_active_owner_until_retry(void) {
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_init(&config));
  stop_failures = 1U;
  TEST_ASSERT_EQUAL(HAL_EHW, hal_pulse_capture_deinit());
  TEST_ASSERT_EQUAL(HAL_EBUSY, hal_pulse_capture_init(&config));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
  TEST_ASSERT_FALSE(retained);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_init(&config));
}

static void prescaled_backends_preserve_full_period_count(void) {
  const uint16_t strides[] = {8U, 32U};
  for (uint16_t stride : strides) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_deinit());
    source_stride = stride;
    next_edge = 0U;
    edges_left = 32U / stride + 1U;
    TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_init(&config));
    hal_pulse_capture_sample_t sample;
    TEST_ASSERT_EQUAL(HAL_OK, hal_pulse_capture_read(&sample));
    TEST_ASSERT_EQUAL_UINT16(32U, sample.periods);
    TEST_ASSERT_EQUAL_UINT32(16000U, sample.ticks);
    TEST_ASSERT_EQUAL_UINT32(1U, sample.sequence);
    TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_pulse_capture_read(&sample));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(prescaled_backends_preserve_full_period_count);
  RUN_TEST(failed_start_still_allows_cleanup_retry);
  RUN_TEST(failed_stop_preserves_active_owner_until_retry);
  return UNITY_END();
}

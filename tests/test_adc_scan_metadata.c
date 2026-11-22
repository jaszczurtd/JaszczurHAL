#include <hal/analog/hal_adc.h>
#include <hal/analog/jh_adc_scan_backend.h>
#include <hal/system/hal_sync.h>
#include <string.h>
#include <utils/unity.h>

struct hal_mutex_impl_t {
  unsigned token;
};
static struct hal_mutex_impl_t s_mutex;
static bool s_busy;
static unsigned s_locks;
static uint32_t s_sequence;
_Alignas(4) static uint16_t s_samples[24];
static hal_adc_scan_config_t s_config;

hal_mutex_t jh_hal_mutex_try_create(void) { return &s_mutex; }
hal_mutex_t hal_mutex_create(void) { return &s_mutex; }
void hal_mutex_destroy(hal_mutex_t mutex) { (void)mutex; }
void hal_mutex_lock(hal_mutex_t mutex) {
  TEST_ASSERT_EQUAL_PTR(&s_mutex, mutex);
  TEST_ASSERT_FALSE(s_busy);
  s_busy = true;
  ++s_locks;
}
bool hal_mutex_try_lock(hal_mutex_t mutex) {
  TEST_ASSERT_EQUAL_PTR(&s_mutex, mutex);
  if (s_busy) {
    return false;
  }
  s_busy = true;
  ++s_locks;
  return true;
}
void hal_mutex_unlock(hal_mutex_t mutex) {
  TEST_ASSERT_EQUAL_PTR(&s_mutex, mutex);
  TEST_ASSERT_TRUE(s_busy);
  s_busy = false;
}
bool hal_adc_is_pin_supported(uint8_t pin) { return pin >= 26U && pin <= 28U; }
hal_status_t jh_adc_scan_start(const hal_adc_scan_config_t *config,
                               uint8_t *positions, uint32_t *period) {
  s_config = *config;
  for (unsigned i = 0; i < config->pin_count; ++i) {
    positions[i] = (uint8_t)i;
  }
  *period = 6000U;
  s_sequence = 0U;
  return HAL_OK;
}
hal_status_t jh_adc_scan_stop(void) { return HAL_OK; }
bool jh_adc_scan_completed(hal_adc_scan_block_t *block) {
  if (s_sequence == 0U) {
    return false;
  }
  jh_adc_scan_describe(block, s_config.buffer, s_config.block_frames,
                       s_config.pin_count, s_sequence, 100U, 0U);
  return true;
}
hal_status_t jh_adc_scan_latest(uint8_t position, uint16_t *raw) {
  *raw = position;
  return HAL_OK;
}
void setUp(void) {
  s_busy = false;
  (void)hal_adc_scan_stop();
}
void tearDown(void) {
  s_busy = false;
  (void)hal_adc_scan_stop();
}

static void test_period_read_does_not_wait_for_adc_owner(void) {
  hal_adc_scan_config_t config = {0};
  config.pins[0] = 26U;
  config.pins[1] = 27U;
  config.pins[2] = 28U;
  config.pin_count = 3U;
  config.block_frames = 4U;
  config.conversion_period_ns = 2000U;
  config.buffer = s_samples;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_start(&config));
  s_busy = true;
  const unsigned locks = s_locks;
  TEST_ASSERT_EQUAL_UINT32(6000U, hal_adc_scan_frame_period_ns());
  TEST_ASSERT_EQUAL_UINT(locks, s_locks);
  s_busy = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_stop());
  s_busy = true;
  TEST_ASSERT_EQUAL_UINT32(0U, hal_adc_scan_frame_period_ns());
  s_busy = false;
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_period_read_does_not_wait_for_adc_owner);
  return UNITY_END();
}

#include "hal/audio/hal_dma_pwm_audio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <atomic>
#include <thread>

static int s_calls;
static uint8_t s_last_index;
static uint16_t *s_last_buffer;

static void buffer_done_cb(void *, uint16_t *buffer, uint8_t buffer_index) {
  s_calls++;
  s_last_index = buffer_index;
  s_last_buffer = buffer;
}

void setUp(void) {
  s_calls = 0;
  s_last_index = 0xFFu;
  s_last_buffer = nullptr;
}

void tearDown(void) {}

void test_pwm_audio_lifecycle_and_callbacks(void) {
  uint16_t buffer_a[4] = {};
  uint16_t buffer_b[4] = {};
  volatile uint16_t adc_buffer[2] = {};
  const uint8_t adc_pins[2] = {26u, 27u};

  hal_dma_pwm_audio_config_t cfg = {};
  cfg.pwm_pin = 6u;
  cfg.sample_rate_hz = 30518u;
  cfg.period_ticks = 4096u;
  cfg.buffer_a = buffer_a;
  cfg.buffer_b = buffer_b;
  cfg.block_size = 4u;
  cfg.idle_value = 2048u;
  cfg.adc_pins = adc_pins;
  cfg.adc_count = 2u;
  cfg.adc_buffer = adc_buffer;
  cfg.buffer_done_cb = buffer_done_cb;

  TEST_ASSERT_TRUE(hal_dma_pwm_audio_supported());
  hal_dma_pwm_audio_t audio = hal_dma_pwm_audio_create(&cfg);
  TEST_ASSERT_NOT_NULL(audio);
  TEST_ASSERT_EQUAL_UINT8(6u, hal_mock_dma_pwm_audio_get_pin(audio));

  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_pause(audio, 1234u));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_resume(audio));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_start(audio));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_start_ex(audio));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_pause(audio, 4096u));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(audio));

  hal_mock_dma_pwm_audio_complete(audio, 0u);
  TEST_ASSERT_EQUAL_INT(1, s_calls);
  TEST_ASSERT_EQUAL_UINT8(0u, s_last_index);
  TEST_ASSERT_EQUAL_PTR(buffer_a, s_last_buffer);

  hal_mock_dma_pwm_audio_complete(audio, 1u);
  TEST_ASSERT_EQUAL_INT(2, s_calls);
  TEST_ASSERT_EQUAL_UINT8(1u, s_last_index);
  TEST_ASSERT_EQUAL_PTR(buffer_b, s_last_buffer);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dma_pwm_audio_pause(audio, 1234u));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_paused(audio));
  TEST_ASSERT_EQUAL_UINT16(1234u, hal_mock_dma_pwm_audio_get_idle_value(audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_pause(audio, 1234u));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_start_ex(audio));
  hal_mock_dma_pwm_audio_complete(audio, 0u);
  TEST_ASSERT_EQUAL_INT(2, s_calls);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dma_pwm_audio_resume(audio));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_resume(audio));
  hal_mock_dma_pwm_audio_complete(audio, 0u);
  TEST_ASSERT_EQUAL_INT(3, s_calls);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dma_pwm_audio_pause(audio, 1234u));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_paused(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dma_pwm_audio_stop(audio));
  TEST_ASSERT_FALSE(hal_dma_pwm_audio_is_running(audio));
  TEST_ASSERT_FALSE(hal_dma_pwm_audio_is_paused(audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_pause(audio, 1234u));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_resume(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dma_pwm_audio_start_ex(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dma_pwm_audio_stop(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dma_pwm_audio_destroy_ex(audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dma_pwm_audio_destroy_ex(audio));
}

void test_pwm_audio_rejects_incomplete_config(void) {
  hal_dma_pwm_audio_config_t cfg = {};
  TEST_ASSERT_NULL(hal_dma_pwm_audio_create(&cfg));
}

void test_pwm_audio_status_variants_report_errors(void) {
  hal_dma_pwm_audio_t audio = nullptr;
  hal_dma_pwm_audio_config_t cfg = {};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_dma_pwm_audio_create_ex(nullptr, &audio));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_create_ex(&cfg, &audio));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_create_ex(&cfg, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_start_ex(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_stop(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_pause(nullptr, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_resume(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_destroy_ex(nullptr));

  uint16_t buffer_a[2] = {};
  uint16_t buffer_b[2] = {};
  volatile uint16_t adc_buffer[1] = {};
  const uint8_t adc_pins[1] = {26u};
  cfg.pwm_pin = 26u;
  cfg.sample_rate_hz = 1000u;
  cfg.period_ticks = 256u;
  cfg.buffer_a = buffer_a;
  cfg.buffer_b = buffer_b;
  cfg.block_size = 2u;
  cfg.adc_pins = adc_pins;
  cfg.adc_count = 1u;
  cfg.adc_buffer = adc_buffer;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_create_ex(&cfg, &audio));
}

void test_pwm_audio_rejects_unportable_hardware_ranges(void) {
  uint16_t buffers[2][2] = {};
  hal_dma_pwm_audio_config_t cfg = {};
  cfg.pwm_pin = 6u;
  cfg.sample_rate_hz = 1000u;
  cfg.period_ticks = 256u;
  cfg.buffer_a = buffers[0];
  cfg.buffer_b = buffers[1];
  cfg.block_size = 2u;
  cfg.idle_value = 128u;

  hal_dma_pwm_audio_t audio = nullptr;
  cfg.period_ticks = 65537u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_create_ex(&cfg, &audio));
  TEST_ASSERT_NULL(audio);

  cfg.period_ticks = 256u;
  cfg.block_size = 32768u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_create_ex(&cfg, &audio));
  TEST_ASSERT_NULL(audio);

  cfg.block_size = 2u;
  cfg.idle_value = cfg.period_ticks;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dma_pwm_audio_create_ex(&cfg, &audio));
  TEST_ASSERT_NULL(audio);
}

void test_pwm_audio_pool_exhaustion_returns_status(void) {
  uint16_t buffers[HAL_DMA_PWM_AUDIO_MAX_CHANNELS + 1u][2][1] = {};
  hal_dma_pwm_audio_t audio[HAL_DMA_PWM_AUDIO_MAX_CHANNELS + 1u] = {};

  for (size_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS + 1u; ++i) {
    hal_dma_pwm_audio_config_t cfg = {};
    cfg.pwm_pin = 6u;
    cfg.sample_rate_hz = 1000u;
    cfg.period_ticks = 256u;
    cfg.buffer_a = buffers[i][0];
    cfg.buffer_b = buffers[i][1];
    cfg.block_size = 1u;
    const hal_status_t expected =
        i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS ? HAL_OK : HAL_ENOMEM;
    TEST_ASSERT_EQUAL_INT(expected,
                          hal_dma_pwm_audio_create_ex(&cfg, &audio[i]));
  }
  TEST_ASSERT_NULL(audio[HAL_DMA_PWM_AUDIO_MAX_CHANNELS]);
  for (size_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    hal_dma_pwm_audio_destroy(audio[i]);
  }
}

void test_pwm_audio_adc_allocation_is_serialized(void) {
  uint16_t output[2][2][1] = {};
  volatile uint16_t adc_output[2][1] = {};
  const uint8_t adc_pins[1] = {26u};
  hal_dma_pwm_audio_t audio[2] = {};
  std::atomic<int> status[2] = {HAL_ESTATE, HAL_ESTATE};

  auto create = [&](size_t index) {
    hal_dma_pwm_audio_config_t cfg = {};
    cfg.pwm_pin = (uint8_t)(6u + index);
    cfg.sample_rate_hz = 1000u;
    cfg.period_ticks = 256u;
    cfg.buffer_a = output[index][0];
    cfg.buffer_b = output[index][1];
    cfg.block_size = 1u;
    cfg.adc_pins = adc_pins;
    cfg.adc_count = 1u;
    cfg.adc_buffer = adc_output[index];
    status[index].store(hal_dma_pwm_audio_create_ex(&cfg, &audio[index]));
  };

  std::thread first(create, 0u);
  std::thread second(create, 1u);
  first.join();
  second.join();

  const int ok_count = (status[0].load() == HAL_OK ? 1 : 0) +
                       (status[1].load() == HAL_OK ? 1 : 0);
  const int busy_count = (status[0].load() == HAL_EBUSY ? 1 : 0) +
                         (status[1].load() == HAL_EBUSY ? 1 : 0);
  TEST_ASSERT_EQUAL_INT(1, ok_count);
  TEST_ASSERT_EQUAL_INT(1, busy_count);
  for (hal_dma_pwm_audio_t handle : audio) {
    if (handle != nullptr) {
      hal_dma_pwm_audio_destroy(handle);
    }
  }
}

void test_interpolate_blends_fraction(void) {
  TEST_ASSERT_EQUAL_UINT16(1500u, hal_dma_interpolate(1000u, 2000u, 128u));
  TEST_ASSERT_EQUAL_UINT16(1996u, hal_dma_interpolate(1000u, 2000u, 255u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pwm_audio_lifecycle_and_callbacks);
  RUN_TEST(test_pwm_audio_rejects_incomplete_config);
  RUN_TEST(test_pwm_audio_status_variants_report_errors);
  RUN_TEST(test_pwm_audio_rejects_unportable_hardware_ranges);
  RUN_TEST(test_pwm_audio_pool_exhaustion_returns_status);
  RUN_TEST(test_pwm_audio_adc_allocation_is_serialized);
  RUN_TEST(test_interpolate_blends_fraction);
  return UNITY_END();
}

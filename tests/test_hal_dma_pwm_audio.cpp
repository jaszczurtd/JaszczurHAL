#include "hal/hal_dma_pwm_audio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

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

  TEST_ASSERT_TRUE(hal_dma_pwm_audio_start(audio));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(audio));

  hal_mock_dma_pwm_audio_complete(audio, 0u);
  TEST_ASSERT_EQUAL_INT(1, s_calls);
  TEST_ASSERT_EQUAL_UINT8(0u, s_last_index);
  TEST_ASSERT_EQUAL_PTR(buffer_a, s_last_buffer);

  hal_mock_dma_pwm_audio_complete(audio, 1u);
  TEST_ASSERT_EQUAL_INT(2, s_calls);
  TEST_ASSERT_EQUAL_UINT8(1u, s_last_index);
  TEST_ASSERT_EQUAL_PTR(buffer_b, s_last_buffer);

  hal_dma_pwm_audio_pause(audio, 1234u);
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_paused(audio));
  TEST_ASSERT_EQUAL_UINT16(1234u, hal_mock_dma_pwm_audio_get_idle_value(audio));
  hal_mock_dma_pwm_audio_complete(audio, 0u);
  TEST_ASSERT_EQUAL_INT(2, s_calls);

  hal_dma_pwm_audio_resume(audio);
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(audio));
  hal_mock_dma_pwm_audio_complete(audio, 0u);
  TEST_ASSERT_EQUAL_INT(3, s_calls);

  hal_dma_pwm_audio_stop(audio);
  TEST_ASSERT_FALSE(hal_dma_pwm_audio_is_running(audio));
  hal_dma_pwm_audio_destroy(audio);
}

void test_pwm_audio_rejects_incomplete_config(void) {
  hal_dma_pwm_audio_config_t cfg = {};
  TEST_ASSERT_NULL(hal_dma_pwm_audio_create(&cfg));
}

void test_interpolate_blends_fraction(void) {
  TEST_ASSERT_EQUAL_UINT16(1500u, hal_dma_interpolate(1000u, 2000u, 128u));
  TEST_ASSERT_EQUAL_UINT16(1996u, hal_dma_interpolate(1000u, 2000u, 255u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pwm_audio_lifecycle_and_callbacks);
  RUN_TEST(test_pwm_audio_rejects_incomplete_config);
  RUN_TEST(test_interpolate_blends_fraction);
  return UNITY_END();
}

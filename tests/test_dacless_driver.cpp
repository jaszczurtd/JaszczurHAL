#include "hal/audio/hal_dacless.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

static uint16_t s_sample_value;
static int s_sample_calls;
static int s_block_calls;

static uint16_t sample_cb(void *) {
  s_sample_calls++;
  return s_sample_value++;
}

static void block_cb(void *, uint16_t *buffer) {
  s_block_calls++;
  buffer[0] = 123u;
  buffer[1] = 124u;
}

void setUp(void) {
  s_sample_value = 1000u;
  s_sample_calls = 0;
  s_block_calls = 0;
  hal_mock_set_micros(0u);
  hal_mock_adc_inject(26u, 0);
  hal_mock_adc_inject(27u, 0);
  hal_mock_adc_inject(28u, 0);
  hal_mock_adc_inject(29u, 0);
  hal_mock_mutex_stats_reset();
  hal_mock_dma_pwm_audio_fail_next_create(false);
  hal_mock_critical_section_reset();
}

void tearDown(void) { TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_depth()); }

void test_default_config_and_sample_rate(void) {
  DAClessAudio audio;
  TEST_ASSERT_TRUE(audio.begin());

  TEST_ASSERT_EQUAL_UINT8(6u, audio.getConfig().pinPWM);
  TEST_ASSERT_EQUAL_UINT16(12u, audio.getConfig().pwmBits);
  TEST_ASSERT_EQUAL_UINT16(128u, audio.getConfig().blockSize);
  TEST_ASSERT_TRUE(audio.getConfig().useDma);
  TEST_ASSERT_TRUE(audio.isDmaActive());
  TEST_ASSERT_NOT_NULL(audio.getDmaAudioForTest());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30517.58f, audio.getSampleRate());
  TEST_ASSERT_EQUAL_UINT8(
      6u, hal_mock_dma_pwm_audio_get_pin(audio.getDmaAudioForTest()));
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(audio.getDmaAudioForTest()));
  TEST_ASSERT_EQUAL_FLOAT(audio.getSampleRate(), audio_rate);
  TEST_ASSERT_EQUAL_PTR(audio.getAdcBuffer(), adc_results_buf);
}

void test_polling_config_creates_pwm_freq_channel(void) {
  DAClessConfig cfg;
  cfg.useDma = false;
  DAClessAudio audio(cfg);
  TEST_ASSERT_TRUE(audio.begin());

  TEST_ASSERT_FALSE(audio.isDmaActive());
  TEST_ASSERT_EQUAL_UINT32(
      30518u, hal_mock_pwm_freq_get_frequency(audio.getPwmChannelForTest()));
  TEST_ASSERT_EQUAL_FLOAT(audio.getSampleRate(), audio_rate);
  TEST_ASSERT_EQUAL_PTR(audio.getAdcBuffer(), adc_results_buf);
}

void test_config_is_clamped_to_static_limits(void) {
  DAClessConfig cfg;
  cfg.pwmBits = 32u;
  cfg.blockSize = DACLESS_MAX_BLOCK_SIZE + 10u;
  cfg.nAdcInputs = DACLESS_MAX_ADC_INPUTS + 2u;

  DAClessAudio audio(cfg);

  TEST_ASSERT_EQUAL_UINT16(16u, audio.getConfig().pwmBits);
  TEST_ASSERT_EQUAL_UINT16(DACLESS_MAX_BLOCK_SIZE, audio.getConfig().blockSize);
  TEST_ASSERT_EQUAL_UINT8(DACLESS_MAX_ADC_INPUTS, audio.getConfig().nAdcInputs);
}

void test_begin_samples_adc_inputs(void) {
  hal_mock_adc_inject(26u, 111);
  hal_mock_adc_inject(27u, 222);

  DAClessConfig cfg;
  cfg.useDma = false;
  cfg.nAdcInputs = 2u;
  DAClessAudio audio(cfg);
  TEST_ASSERT_TRUE(audio.begin());

  TEST_ASSERT_EQUAL_UINT16(111u, audio.getADC(0u));
  TEST_ASSERT_EQUAL_UINT16(222u, audio.getADC(1u));
  TEST_ASSERT_EQUAL_UINT16(0u, audio.getADC(2u));
}

void test_sample_callback_fills_finished_buffer_after_block_rollover(void) {
  DAClessConfig cfg;
  cfg.useDma = false;
  cfg.blockSize = 2u;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);
  audio.setSampleCallback(sample_cb, nullptr);
  TEST_ASSERT_TRUE(audio.begin());

  audio.service();
  hal_mock_advance_micros(33u);
  audio.service();

  const volatile uint16_t *buf = audio.getOutBufPtr();
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_EQUAL_UINT16(1000u, buf[0]);
  TEST_ASSERT_EQUAL_UINT16(1001u, buf[1]);
  TEST_ASSERT_EQUAL_INT(2, s_sample_calls);
  TEST_ASSERT_EQUAL_PTR(buf, out_buf_ptr);
}

void test_block_callback_takes_precedence_over_sample_callback(void) {
  DAClessConfig cfg;
  cfg.useDma = false;
  cfg.blockSize = 2u;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);
  audio.setSampleCallback(sample_cb, nullptr);
  audio.setBlockCallback(block_cb, nullptr);
  TEST_ASSERT_TRUE(audio.begin());

  audio.service();
  hal_mock_advance_micros(33u);
  audio.service();

  const volatile uint16_t *buf = audio.getOutBufPtr();
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_EQUAL_UINT16(123u, buf[0]);
  TEST_ASSERT_EQUAL_UINT16(124u, buf[1]);
  TEST_ASSERT_EQUAL_INT(0, s_sample_calls);
  TEST_ASSERT_EQUAL_INT(1, s_block_calls);
}

void test_non_owner_instance_does_not_clobber_out_buf_global(void) {
  DAClessConfig owner_cfg;
  owner_cfg.useDma = false;
  owner_cfg.blockSize = 2u;
  owner_cfg.nAdcInputs = 0u;
  DAClessAudio owner(owner_cfg);
  TEST_ASSERT_TRUE(owner.begin());

  owner.service();
  hal_mock_advance_micros(33u);
  owner.service();

  const volatile uint16_t *owner_buf = owner.getOutBufPtr();
  TEST_ASSERT_NOT_NULL(owner_buf);
  TEST_ASSERT_EQUAL_PTR(owner_buf, out_buf_ptr);

  DAClessConfig other_cfg;
  other_cfg.useDma = false;
  other_cfg.blockSize = 2u;
  other_cfg.nAdcInputs = 0u;
  DAClessAudio other(other_cfg);
  TEST_ASSERT_TRUE(other.begin());

  other.service();
  hal_mock_advance_micros(33u);
  other.service();

  TEST_ASSERT_NOT_NULL(other.getOutBufPtr());
  TEST_ASSERT_NOT_EQUAL(owner_buf, other.getOutBufPtr());
  TEST_ASSERT_EQUAL_PTR(owner_buf, out_buf_ptr);
}

void test_polling_service_clamps_large_catchup_burst(void) {
  DAClessConfig cfg;
  cfg.useDma = false;
  cfg.blockSize = 128u;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);
  audio.setSampleCallback(sample_cb, nullptr);
  TEST_ASSERT_TRUE(audio.begin());

  hal_mock_advance_micros(1000000u);
  audio.service();

  TEST_ASSERT_LESS_OR_EQUAL_INT((int)DACLESS_MAX_POLLING_CATCHUP_SAMPLES + 1,
                                s_sample_calls);
}

void test_dma_sample_callback_fills_completed_buffer(void) {
  DAClessConfig cfg;
  cfg.blockSize = 2u;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);
  audio.setSampleCallback(sample_cb, nullptr);
  TEST_ASSERT_TRUE(audio.begin());

  hal_mock_dma_pwm_audio_complete(audio.getDmaAudioForTest(), 0u);

  const volatile uint16_t *buf = audio.getOutBufPtr();
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_EQUAL_UINT16(1000u, buf[0]);
  TEST_ASSERT_EQUAL_UINT16(1001u, buf[1]);
  TEST_ASSERT_EQUAL_INT(2, s_sample_calls);
  TEST_ASSERT_EQUAL_UINT32(
      1u, hal_mock_dma_pwm_audio_completion_count(audio.getDmaAudioForTest()));
  TEST_ASSERT_EQUAL_PTR(buf, out_buf_ptr);
}

void test_dma_block_callback_takes_precedence(void) {
  DAClessConfig cfg;
  cfg.blockSize = 2u;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);
  audio.setSampleCallback(sample_cb, nullptr);
  audio.setBlockCallback(block_cb, nullptr);
  TEST_ASSERT_TRUE(audio.begin());

  hal_mock_dma_pwm_audio_complete(audio.getDmaAudioForTest(), 1u);

  const volatile uint16_t *buf = audio.getOutBufPtr();
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_EQUAL_UINT16(123u, buf[0]);
  TEST_ASSERT_EQUAL_UINT16(124u, buf[1]);
  TEST_ASSERT_EQUAL_INT(0, s_sample_calls);
  TEST_ASSERT_EQUAL_INT(1, s_block_calls);
}

void test_mute_stops_pwm_and_unmute_restarts_on_service(void) {
  DAClessConfig cfg;
  cfg.useDma = false;
  cfg.blockSize = 2u;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);
  TEST_ASSERT_TRUE(audio.begin());

  audio.service();
  TEST_ASSERT_TRUE(hal_mock_pwm_freq_is_running(audio.getPwmChannelForTest()));

  audio.mute();
  TEST_ASSERT_TRUE(audio.isMuted());
  TEST_ASSERT_FALSE(hal_mock_pwm_freq_is_running(audio.getPwmChannelForTest()));
  TEST_ASSERT_EQUAL_INT(
      2048, hal_mock_pwm_freq_get_value(audio.getPwmChannelForTest()));

  audio.unmute();
  audio.service();
  TEST_ASSERT_TRUE(audio.isRunning());
  TEST_ASSERT_TRUE(hal_mock_pwm_freq_is_running(audio.getPwmChannelForTest()));
}

void test_dma_mute_pauses_and_unmute_resumes_backend(void) {
  DAClessConfig cfg;
  cfg.blockSize = 2u;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);
  TEST_ASSERT_TRUE(audio.begin());

  hal_dma_pwm_audio_t dma = audio.getDmaAudioForTest();
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(dma));

  audio.mute();
  TEST_ASSERT_TRUE(audio.isMuted());
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_paused(dma));
  TEST_ASSERT_EQUAL_UINT16(2048u, hal_mock_dma_pwm_audio_get_idle_value(dma));

  audio.unmute();
  TEST_ASSERT_TRUE(audio.isRunning());
  TEST_ASSERT_TRUE(hal_dma_pwm_audio_is_running(dma));
}

void test_begin_reports_dma_backend_create_failure(void) {
  DAClessConfig cfg;
  cfg.nAdcInputs = 0u;
  DAClessAudio audio(cfg);

  hal_mock_dma_pwm_audio_fail_next_create(true);

  TEST_ASSERT_FALSE(audio.begin());
  TEST_ASSERT_FALSE(audio.isRunning());
  TEST_ASSERT_FALSE(audio.isDmaActive());
  TEST_ASSERT_TRUE(audio.isMuted());
  TEST_ASSERT_NULL(audio.getDmaAudioForTest());
}

void test_interpolate_blends_fraction(void) {
  TEST_ASSERT_EQUAL_UINT16(1000u, interpolate(1000u, 2000u, 0u));
  TEST_ASSERT_EQUAL_UINT16(1500u, interpolate(1000u, 2000u, 128u));
  TEST_ASSERT_EQUAL_UINT16(1996u, interpolate(1000u, 2000u, 255u));
  TEST_ASSERT_EQUAL_UINT16(1500u, interpolate(2000u, 1000u, 128u));
}

void test_public_methods_balance_mutex_locking(void) {
  {
    DAClessConfig cfg;
    cfg.useDma = false;
    cfg.blockSize = 2u;
    DAClessAudio audio(cfg);
    TEST_ASSERT_TRUE(audio.begin());
    audio.getADC(0u);
    audio.setSampleCallback(sample_cb, nullptr);
    audio.mute();
    audio.unmute();
    audio.service();
  }

  TEST_ASSERT_EQUAL_UINT32(hal_mock_mutex_lock_count(),
                           hal_mock_mutex_unlock_count());
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(2u, hal_mock_mutex_max_depth());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_and_sample_rate);
  RUN_TEST(test_polling_config_creates_pwm_freq_channel);
  RUN_TEST(test_config_is_clamped_to_static_limits);
  RUN_TEST(test_begin_samples_adc_inputs);
  RUN_TEST(test_sample_callback_fills_finished_buffer_after_block_rollover);
  RUN_TEST(test_block_callback_takes_precedence_over_sample_callback);
  RUN_TEST(test_non_owner_instance_does_not_clobber_out_buf_global);
  RUN_TEST(test_polling_service_clamps_large_catchup_burst);
  RUN_TEST(test_dma_sample_callback_fills_completed_buffer);
  RUN_TEST(test_dma_block_callback_takes_precedence);
  RUN_TEST(test_mute_stops_pwm_and_unmute_restarts_on_service);
  RUN_TEST(test_dma_mute_pauses_and_unmute_resumes_backend);
  RUN_TEST(test_begin_reports_dma_backend_create_failure);
  RUN_TEST(test_interpolate_blends_fraction);
  RUN_TEST(test_public_methods_balance_mutex_locking);
  return UNITY_END();
}

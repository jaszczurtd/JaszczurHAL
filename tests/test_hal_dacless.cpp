#include "hal/audio/hal_dacless.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <atomic>
#include <stddef.h>
#include <thread>

typedef struct {
  uint16_t next_sample;
  uint16_t callback_count;
} sample_context_t;

typedef struct {
  uint16_t callback_count;
  uint16_t last_sample_count;
} block_context_t;

typedef struct {
  std::atomic<bool> entered;
  std::atomic<bool> release;
} delayed_block_context_t;

static uint16_t sample_callback(void *user) {
  sample_context_t *context = static_cast<sample_context_t *>(user);
  ++context->callback_count;
  return context->next_sample++;
}

static uint16_t oversized_sample_callback(void *user) {
  sample_context_t *context = static_cast<sample_context_t *>(user);
  ++context->callback_count;
  return context->next_sample;
}

static void block_callback(void *user, uint16_t *buffer,
                           uint16_t sample_count) {
  block_context_t *context = static_cast<block_context_t *>(user);
  ++context->callback_count;
  context->last_sample_count = sample_count;
  for (uint16_t index = 0u; index < sample_count; ++index) {
    buffer[index] = (uint16_t)(700u + index);
  }
}

static void delayed_block_callback(void *user, uint16_t *buffer,
                                   uint16_t sample_count) {
  delayed_block_context_t *context =
      static_cast<delayed_block_context_t *>(user);
  buffer[0] = 901u;
  context->entered.store(true, std::memory_order_release);
  while (!context->release.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  for (uint16_t index = 1u; index < sample_count; ++index) {
    buffer[index] = (uint16_t)(901u + index);
  }
}

void setUp(void) {
  hal_mock_set_micros(0u);
  hal_mock_set_in_isr(false);
  hal_mock_adc_inject(26u, 0);
  hal_mock_adc_inject(27u, 0);
  hal_mock_adc_inject(28u, 0);
  hal_mock_adc_inject(29u, 0);
  hal_mock_dma_pwm_audio_fail_next_create(false);
  hal_mock_dma_pwm_audio_fail_next_pause(false);
  hal_mock_dma_pwm_audio_fail_next_resume(false);
  hal_mock_mutex_fail_next_create(false);
  hal_mock_critical_section_reset();
}

void tearDown(void) {
  hal_mock_set_in_isr(false);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_critical_depth());
}

void test_c_config_defaults_and_normalization(void) {
  hal_dacless_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_config_init(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_config_init(&config));
  TEST_ASSERT_EQUAL_UINT8(DACLESS_DEFAULT_PWM_PIN, config.pwm_pin);
  TEST_ASSERT_EQUAL_UINT16(12u, config.pwm_bits);
  TEST_ASSERT_EQUAL_UINT16(128u, config.block_size);
  TEST_ASSERT_EQUAL_UINT8(DACLESS_MAX_DMA_ADC_INPUTS, config.adc_input_count);
  TEST_ASSERT_TRUE(config.use_dma);

  config.pwm_bits = 99u;
  config.block_size = 0u;
  config.adc_input_count = DACLESS_MAX_ADC_INPUTS + 1u;
  config.use_dma = false;
  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));

  hal_dacless_config_t effective = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_config(audio, &effective));
  TEST_ASSERT_EQUAL_UINT16(16u, effective.pwm_bits);
  TEST_ASSERT_EQUAL_UINT16(1u, effective.block_size);
  TEST_ASSERT_EQUAL_UINT8(DACLESS_MAX_ADC_INPUTS, effective.adc_input_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));

  config = hal_dacless_default_config();
  config.pwm_pin = 64u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_create(&config, &audio));
  TEST_ASSERT_NULL(audio);

  config = hal_dacless_default_config();
  config.adc_input_count = 1u;
  config.adc_pins[0] = 64u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_create(&config, &audio));
  TEST_ASSERT_NULL(audio);

  config = hal_dacless_default_config();
  config.adc_input_count = 1u;
  config.adc_pins[0] = config.pwm_pin;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_create(&config, &audio));
  TEST_ASSERT_NULL(audio);
}

void test_cpp_status_api_rejects_pwm_adc_pin_overlap(void) {
  DAClessConfig config;
  config.pinPWM = config.adcPins[0];
  config.nAdcInputs = 1u;
  config.useDma = true;
  DAClessAudio audio(config);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, audio.beginEx());
  TEST_ASSERT_FALSE(audio.isBegun());
}

void test_c_create_reports_mutex_allocation_failure(void) {
  hal_dacless_t audio = NULL;
  hal_mock_mutex_fail_next_create(true);
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_dacless_create(NULL, &audio));
  TEST_ASSERT_NULL(audio);
}

void test_c_polling_begin_reports_pwm_pool_exhaustion(void) {
  hal_pwm_freq_channel_t blockers[HAL_PWM_FREQ_MAX_CHANNELS] = {};
  const int channel_count = hal_get_config()->pwm_freq_max_channels;
  TEST_ASSERT_GREATER_THAN(0, channel_count);
  TEST_ASSERT_LESS_OR_EQUAL_INT(HAL_PWM_FREQ_MAX_CHANNELS, channel_count);

  for (int index = 0; index < channel_count; ++index) {
    blockers[index] = hal_pwm_freq_create(0u, 1000u, 1024u);
    TEST_ASSERT_NOT_NULL(blockers[index]);
  }

  hal_dacless_config_t config = hal_dacless_default_config();
  config.use_dma = false;
  config.adc_input_count = 0u;
  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_dacless_begin(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));

  for (int index = 0; index < channel_count; ++index) {
    hal_pwm_freq_destroy(blockers[index]);
  }
}

void test_c_polling_lifecycle_state_and_adc(void) {
  hal_mock_adc_inject(26u, 321);
  hal_dacless_config_t config = hal_dacless_default_config();
  config.use_dma = false;
  config.block_size = 2u;
  config.adc_input_count = 1u;

  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dacless_service(audio));

  hal_dacless_state_t state = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_FALSE(state.started);
  TEST_ASSERT_FALSE(state.running);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_TRUE(state.started);
  TEST_ASSERT_TRUE(state.running);
  TEST_ASSERT_FALSE(state.muted);
  TEST_ASSERT_FALSE(state.dma_active);

  uint16_t adc = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_adc(audio, 0u, &adc));
  TEST_ASSERT_EQUAL_UINT16(321u, adc);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_adc(audio, 1u, &adc));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_adc(audio, 0u, NULL));

  hal_mock_set_in_isr(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_adc(audio, 0u, &adc));
  TEST_ASSERT_EQUAL_UINT16(321u, adc);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_adc(audio, 1u, &adc));
  hal_mock_set_in_isr(false);

  const volatile uint16_t *adc_buffer = NULL;
  uint8_t adc_count = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_dacless_get_adc_buffer(audio, &adc_buffer, &adc_count));
  TEST_ASSERT_NOT_NULL(adc_buffer);
  TEST_ASSERT_EQUAL_UINT8(1u, adc_count);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_mute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_TRUE(state.muted);
  TEST_ASSERT_FALSE(state.running);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_unmute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_TRUE(state.running);

  float sample_rate = 0.0f;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_dacless_get_sample_rate(audio, &sample_rate));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30517.58f, sample_rate);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_state(audio, &state));
}

void test_c_callbacks_keep_independent_contexts(void) {
  hal_dacless_config_t config = hal_dacless_default_config();
  config.use_dma = false;
  config.block_size = 2u;
  config.adc_input_count = 0u;
  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));

  sample_context_t sample = {1000u, 0u};
  block_context_t block = {0u, 0u};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_dacless_set_sample_callback(audio, sample_callback, &sample));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_dacless_set_block_callback(audio, block_callback, &block));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_service(audio));
  hal_mock_advance_micros(33u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_service(audio));
  TEST_ASSERT_EQUAL_UINT16(1u, block.callback_count);
  TEST_ASSERT_EQUAL_UINT16(2u, block.last_sample_count);
  TEST_ASSERT_EQUAL_UINT16(0u, sample.callback_count);

  const volatile uint16_t *output = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_output_buffer(audio, &output));
  TEST_ASSERT_NOT_NULL(output);
  TEST_ASSERT_EQUAL_UINT16(700u, output[0]);
  TEST_ASSERT_EQUAL_UINT16(701u, output[1]);

  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_dacless_set_block_callback(audio, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_dacless_set_sample_callback(audio, sample_callback, &sample));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_dacless_set_block_callback(audio, block_callback, &block));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_dacless_set_block_callback(audio, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));
  hal_mock_advance_micros(33u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_service(audio));
  hal_mock_advance_micros(33u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_service(audio));
  TEST_ASSERT_EQUAL_UINT16(2u, sample.callback_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
}

void test_c_output_buffer_is_published_after_callback_finishes(void) {
  hal_dacless_config_t config = hal_dacless_default_config();
  config.use_dma = false;
  config.block_size = 4u;
  config.adc_input_count = 0u;
  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));

  delayed_block_context_t callback_context = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_dacless_set_block_callback(audio, delayed_block_callback,
                                             &callback_context));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));

  hal_mock_set_micros(200u);
  std::atomic<int> service_status{HAL_ESTATE};
  std::thread completion([audio, &service_status]() {
    service_status.store(hal_dacless_service(audio));
  });
  while (!callback_context.entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  const volatile uint16_t *completed = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_dacless_get_output_buffer(audio, &completed));
  TEST_ASSERT_NULL(completed);

  callback_context.release.store(true, std::memory_order_release);
  completion.join();
  TEST_ASSERT_EQUAL_INT(HAL_OK, service_status.load());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_dacless_get_output_buffer(audio, &completed));
  TEST_ASSERT_NOT_NULL(completed);
  TEST_ASSERT_EQUAL_UINT16(901u, completed[0]);
  TEST_ASSERT_EQUAL_UINT16(902u, completed[1]);
  TEST_ASSERT_EQUAL_UINT16(903u, completed[2]);
  TEST_ASSERT_EQUAL_UINT16(904u, completed[3]);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
}

void test_c_control_operations_are_serialized(void) {
  hal_dacless_config_t config = hal_dacless_default_config();
  config.use_dma = false;
  config.block_size = 2u;
  config.adc_input_count = 0u;
  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));

  std::atomic<int> failures{0};
  sample_context_t sample = {1000u, 0u};
  block_context_t block = {0u, 0u};
  auto sample_setter = [&]() {
    for (unsigned index = 0u; index < 200u; ++index) {
      if (hal_dacless_set_sample_callback(audio, NULL, NULL) != HAL_OK ||
          hal_dacless_set_sample_callback(audio, sample_callback, &sample) !=
              HAL_OK) {
        ++failures;
      }
      std::this_thread::yield();
    }
  };
  auto block_setter = [&]() {
    for (unsigned index = 0u; index < 200u; ++index) {
      if (hal_dacless_set_block_callback(audio, NULL, NULL) != HAL_OK ||
          hal_dacless_set_block_callback(audio, block_callback, &block) !=
              HAL_OK) {
        ++failures;
      }
      std::this_thread::yield();
    }
  };
  std::thread sample_thread(sample_setter);
  std::thread block_thread(block_setter);
  sample_thread.join();
  block_thread.join();
  TEST_ASSERT_EQUAL_INT(0, failures.load());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dacless_set_sample_callback(
                                        audio, sample_callback, &sample));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_dacless_set_block_callback(
                                        audio, block_callback, &block));

  auto mute_worker = [&]() {
    for (unsigned index = 0u; index < 200u; ++index) {
      if (hal_dacless_mute(audio) != HAL_OK) {
        ++failures;
      }
    }
  };
  auto unmute_worker = [&]() {
    for (unsigned index = 0u; index < 200u; ++index) {
      if (hal_dacless_unmute(audio) != HAL_OK) {
        ++failures;
      }
    }
  };
  std::thread muter(mute_worker);
  std::thread unmuter(unmute_worker);
  muter.join();
  unmuter.join();
  TEST_ASSERT_EQUAL_INT(0, failures.load());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_mute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_mute(audio));
  hal_dacless_state_t state = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_TRUE(state.muted);
  TEST_ASSERT_FALSE(state.running);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_unmute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_unmute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_FALSE(state.muted);
  TEST_ASSERT_TRUE(state.running);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_service(audio));
  hal_mock_advance_micros(33u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_service(audio));
  TEST_ASSERT_EQUAL_UINT16(1u, block.callback_count);
  TEST_ASSERT_EQUAL_UINT16(2u, block.last_sample_count);
  TEST_ASSERT_EQUAL_UINT16(0u, sample.callback_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
}

void test_c_dma_failure_and_interpolation_statuses(void) {
  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(NULL, &audio));
  hal_mock_dma_pwm_audio_fail_next_create(true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_dacless_begin(audio));

  hal_dacless_state_t state = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_FALSE(state.started);
  TEST_ASSERT_TRUE(state.muted);
  TEST_ASSERT_FALSE(state.dma_active);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));
  hal_mock_dma_pwm_audio_fail_next_create(true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_dacless_begin(audio));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_dacless_set_sample_callback(audio, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_dacless_set_block_callback(audio, NULL, NULL));
  TEST_ASSERT_EQUAL_UINT16(1500u, hal_dacless_interpolate(1000u, 2000u, 128u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
}

void test_c_dma_control_propagates_backend_errors(void) {
  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(NULL, &audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));

  hal_dacless_state_t state = {};
  hal_mock_dma_pwm_audio_fail_next_pause(true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_dacless_mute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_FALSE(state.muted);
  TEST_ASSERT_TRUE(state.running);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_mute(audio));
  hal_mock_dma_pwm_audio_fail_next_resume(true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_dacless_unmute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_state(audio, &state));
  TEST_ASSERT_TRUE(state.muted);
  TEST_ASSERT_FALSE(state.running);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_unmute(audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
}

void test_c_dma_sample_callback_clamps_to_pwm_range(void) {
  hal_dacless_config_t config = hal_dacless_default_config();
  config.pwm_bits = 8u;
  config.block_size = 2u;
  config.adc_input_count = 0u;

  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(&config, &audio));
  sample_context_t sample = {1000u, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_set_sample_callback(
                                    audio, oversized_sample_callback, &sample));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(audio));

  hal_dma_pwm_audio_t dma = hal_mock_dma_pwm_audio_find_by_pin(config.pwm_pin);
  TEST_ASSERT_NOT_NULL(dma);
  hal_mock_dma_pwm_audio_complete(dma, 0u);

  const volatile uint16_t *output = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_get_output_buffer(audio, &output));
  TEST_ASSERT_NOT_NULL(output);
  TEST_ASSERT_EQUAL_UINT16(255u, output[0]);
  TEST_ASSERT_EQUAL_UINT16(255u, output[1]);
  TEST_ASSERT_EQUAL_UINT16(2u, sample.callback_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
}

void test_c_dma_adc_scan_is_exclusive(void) {
  hal_dacless_t first = NULL;
  hal_dacless_t second = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(NULL, &first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(NULL, &second));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_begin(first));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_dacless_begin(second));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(second));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(first));
}

void test_c_pool_accounts_for_legacy_cpp_instances(void) {
  {
    DAClessAudio legacy[DACLESS_MAX_INSTANCES];
    hal_dacless_t audio = NULL;
    TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_dacless_create(NULL, &audio));
    TEST_ASSERT_NULL(audio);
  }

  hal_dacless_t audio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(NULL, &audio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(audio));
}

void test_c_pool_exhaustion_and_stale_handles(void) {
  hal_dacless_t handles[DACLESS_MAX_INSTANCES] = {};
  for (size_t index = 0u; index < DACLESS_MAX_INSTANCES; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(NULL, &handles[index]));
  }
  hal_dacless_t extra = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_dacless_create(NULL, &extra));
  TEST_ASSERT_NULL(extra);

  const hal_dacless_t stale = handles[0];
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(handles[0]));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_create(NULL, &handles[0]));
  TEST_ASSERT_NOT_EQUAL(stale, handles[0]);
  hal_dacless_state_t state = {};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_state(stale, &state));

  for (size_t index = 0u; index < DACLESS_MAX_INSTANCES; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_dacless_destroy(handles[index]));
  }
}

void test_c_invalid_arguments_are_rejected(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_create(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_begin(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_destroy(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_service(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_mute(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_unmute(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_dacless_set_sample_callback(NULL, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_dacless_set_block_callback(NULL, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_sample_rate(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_config(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_output_buffer(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_dacless_get_adc_buffer(NULL, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_dacless_get_state(NULL, NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_c_config_defaults_and_normalization);
  RUN_TEST(test_cpp_status_api_rejects_pwm_adc_pin_overlap);
  RUN_TEST(test_c_create_reports_mutex_allocation_failure);
  RUN_TEST(test_c_polling_begin_reports_pwm_pool_exhaustion);
  RUN_TEST(test_c_polling_lifecycle_state_and_adc);
  RUN_TEST(test_c_callbacks_keep_independent_contexts);
  RUN_TEST(test_c_output_buffer_is_published_after_callback_finishes);
  RUN_TEST(test_c_control_operations_are_serialized);
  RUN_TEST(test_c_dma_failure_and_interpolation_statuses);
  RUN_TEST(test_c_dma_control_propagates_backend_errors);
  RUN_TEST(test_c_dma_sample_callback_clamps_to_pwm_range);
  RUN_TEST(test_c_dma_adc_scan_is_exclusive);
  RUN_TEST(test_c_pool_accounts_for_legacy_cpp_instances);
  RUN_TEST(test_c_pool_exhaustion_and_stale_handles);
  RUN_TEST(test_c_invalid_arguments_are_rejected);
  return UNITY_END();
}

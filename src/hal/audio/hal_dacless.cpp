#include "hal/audio/hal_dacless.h"
#include "hal/core/hal_compiler.h"

#if defined(HAL_ENABLE_DACLESS)

#include "hal/analog/hal_adc.h"
#include "hal/audio/dacless/dacless.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/gpio/hal_pwm.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <new>
#include <stddef.h>
#include <string.h>

#define JH_DACLESS_HANDLE_KIND 19u

typedef struct {
  alignas(DAClessAudio) unsigned char storage[sizeof(DAClessAudio)];
  DAClessAudio *driver;
  hal_dacless_t handle;
  hal_dacless_sample_callback_t sample_callback;
  void *sample_context;
  hal_dacless_block_callback_t block_callback;
  void *block_context;
  uint16_t block_size;
  uint16_t silence_sample;
  uint16_t max_sample;
  const volatile uint16_t *completed_output_buffer;
  hal_mutex_t control_mutex;
  bool allocated;
  bool started;
  bool callbacks_frozen;
} jh_dacless_context_t;

typedef struct {
  jh_handle_lease_t lease;
  jh_dacless_context_t *context;
} jh_dacless_operation_t;

static jh_dacless_context_t s_contexts[DACLESS_MAX_INSTANCES] = {};
static jh_handle_slot_t s_handle_slots[DACLESS_MAX_INSTANCES] = {};
static jh_handle_pool_t s_handle_pool = {};
static hal_mutex_t s_pool_mutex = NULL;
static bool s_pool_initialized = false;

static hal_status_t pool_lock(void) {
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&s_pool_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    const hal_status_t status =
        jh_handle_pool_init(&s_handle_pool, s_handle_slots,
                            DACLESS_MAX_INSTANCES, JH_DACLESS_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static DAClessConfig native_config(const hal_dacless_config_t *config) {
  DAClessConfig native;
  native.pinPWM = config->pwm_pin;
  native.pwmBits = config->pwm_bits;
  native.blockSize = config->block_size;
  native.nAdcInputs = config->adc_input_count;
  native.useDma = config->use_dma;
  for (uint8_t index = 0u; index < DACLESS_MAX_ADC_INPUTS; ++index) {
    native.adcPins[index] = config->adc_pins[index];
  }
  return native;
}

static hal_dacless_config_t public_config(const DAClessConfig &config) {
  hal_dacless_config_t result = {};
  result.pwm_pin = config.pinPWM;
  result.pwm_bits = config.pwmBits;
  result.block_size = config.blockSize;
  result.adc_input_count = config.nAdcInputs;
  result.use_dma = config.useDma;
  for (uint8_t index = 0u; index < DACLESS_MAX_ADC_INPUTS; ++index) {
    result.adc_pins[index] = config.adcPins[index];
  }
  return result;
}

static hal_status_t validate_config_pins(const hal_dacless_config_t *config) {
  if (!hal_pwm_is_pin_supported(config->pwm_pin)) {
    return HAL_EINVAL;
  }
  const uint8_t adc_count = config->adc_input_count < DACLESS_MAX_ADC_INPUTS
                                ? config->adc_input_count
                                : DACLESS_MAX_ADC_INPUTS;
  if (config->use_dma && adc_count > DACLESS_MAX_DMA_ADC_INPUTS) {
    return HAL_EINVAL;
  }
  for (uint8_t index = 0u; index < adc_count; ++index) {
    if (config->adc_pins[index] == config->pwm_pin ||
        !hal_adc_is_pin_supported(config->adc_pins[index])) {
      return HAL_EINVAL;
    }
#if HAL_TARGET_IS_RP
    if (config->use_dma && config->adc_pins[index] != (uint8_t)(26u + index)) {
      return HAL_EINVAL;
    }
#endif
  }
  return HAL_OK;
}

static void finalize_context(void *token) {
  if (token == NULL) {
    return;
  }
  jh_dacless_context_t *context = static_cast<jh_dacless_context_t *>(token);
  if (context->driver != NULL) {
    context->driver->~DAClessAudio();
    context->driver = NULL;
  }
  if (context->control_mutex != NULL) {
    hal_mutex_destroy(context->control_mutex);
    context->control_mutex = NULL;
  }
  if (pool_lock() != HAL_OK) {
    return;
  }
  memset(context, 0, sizeof(*context));
  pool_unlock();
}

static hal_status_t begin_operation(hal_dacless_t audio,
                                    jh_dacless_operation_t *operation) {
  if (operation == NULL) {
    return HAL_EINVAL;
  }
  memset(operation, 0, sizeof(*operation));
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_handle_acquire(&s_handle_pool, audio, &operation->lease);
  if (status == HAL_OK) {
    operation->context =
        static_cast<jh_dacless_context_t *>(operation->lease.token);
    if (!operation->context->allocated || operation->context->driver == NULL) {
      void *deferred = NULL;
      (void)jh_handle_end_operation(&s_handle_pool, &operation->lease,
                                    &deferred);
      operation->context = NULL;
      status = HAL_EINVAL;
    }
  } else {
    status = HAL_EINVAL;
  }
  pool_unlock();
  return status;
}

static hal_status_t finish_operation(jh_dacless_operation_t *operation,
                                     hal_status_t status) {
  void *deferred = NULL;
  const hal_status_t lock_status = pool_lock();
  if (lock_status != HAL_OK) {
    return status == HAL_OK ? lock_status : status;
  }
  const hal_status_t end_status =
      jh_handle_end_operation(&s_handle_pool, &operation->lease, &deferred);
  pool_unlock();
  finalize_context(deferred);
  return status == HAL_OK ? end_status : status;
}

static bool context_started(const jh_dacless_context_t *context) {
  return HAL_ATOMIC_LOAD(&context->started, HAL_ATOMIC_ACQUIRE);
}

static void block_callback_thunk(void *user, uint16_t *buffer) {
  jh_dacless_context_t *context = static_cast<jh_dacless_context_t *>(user);
  if (context == NULL || buffer == NULL) {
    return;
  }
  const uint16_t block_size = context->block_size;
  if (context->block_callback != NULL) {
    context->block_callback(context->block_context, buffer, block_size);
  } else if (context->sample_callback != NULL) {
    for (uint16_t i = 0u; i < block_size; ++i) {
      const uint16_t sample = context->sample_callback(context->sample_context);
      buffer[i] = sample > context->max_sample ? context->max_sample : sample;
    }
  } else {
    for (uint16_t i = 0u; i < block_size; ++i) {
      buffer[i] = context->silence_sample;
    }
  }
  HAL_ATOMIC_STORE(&context->completed_output_buffer,
                   static_cast<const volatile uint16_t *>(buffer),
                   HAL_ATOMIC_RELEASE);
}

static hal_status_t apply_callbacks(jh_dacless_context_t *context) {
  hal_status_t status = context->driver->setSampleCallbackEx(NULL, context);
  if (status != HAL_OK) {
    return status;
  }
  return context->driver->setBlockCallbackEx(block_callback_thunk, context);
}

static void control_lock(jh_dacless_context_t *context) {
  hal_mutex_lock(context->control_mutex);
}

static void control_unlock(jh_dacless_context_t *context) {
  hal_mutex_unlock(context->control_mutex);
}

static hal_status_t get_adc_from_isr(hal_dacless_t audio, uint8_t channel,
                                     uint16_t *out_value) {
  void *token = NULL;
  if (jh_handle_resolve(&s_handle_pool, audio, &token, NULL) != HAL_OK) {
    return HAL_EINVAL;
  }
  jh_dacless_context_t *context = static_cast<jh_dacless_context_t *>(token);
  if (!context->allocated || context->driver == NULL ||
      channel >= context->driver->getConfig().nAdcInputs) {
    return HAL_EINVAL;
  }
  *out_value = context->driver->getADC(channel);
  return HAL_OK;
}

hal_dacless_config_t hal_dacless_default_config(void) {
  const DAClessConfig native;
  hal_dacless_config_t config = public_config(native);
  config.adc_input_count = DACLESS_MAX_DMA_ADC_INPUTS;
  return config;
}

hal_status_t hal_dacless_config_init(hal_dacless_config_t *config) {
  if (config == NULL) {
    return HAL_EINVAL;
  }
  *config = hal_dacless_default_config();
  return HAL_OK;
}

hal_status_t hal_dacless_create(const hal_dacless_config_t *config,
                                hal_dacless_t *out_audio) {
  if (out_audio == NULL) {
    return HAL_EINVAL;
  }
  *out_audio = NULL;
  hal_dacless_config_t effective =
      config != NULL ? *config : hal_dacless_default_config();
  hal_status_t status = validate_config_pins(&effective);
  if (status != HAL_OK) {
    return status;
  }

  status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  jh_dacless_context_t *context = NULL;
  for (size_t index = 0u; index < DACLESS_MAX_INSTANCES; ++index) {
    if (!s_contexts[index].allocated) {
      context = &s_contexts[index];
      break;
    }
  }
  if (context == NULL) {
    pool_unlock();
    return HAL_ENOMEM;
  }

  memset(context, 0, sizeof(*context));
  context->allocated = true;
  context->control_mutex = jh_hal_mutex_try_create();
  if (context->control_mutex == NULL) {
    memset(context, 0, sizeof(*context));
    pool_unlock();
    return HAL_ENOMEM;
  }
  context->driver =
      new (context->storage) DAClessAudio(native_config(&effective));
  if (!context->driver->isRegistered()) {
    context->driver->~DAClessAudio();
    hal_mutex_destroy(context->control_mutex);
    memset(context, 0, sizeof(*context));
    pool_unlock();
    return HAL_ENOMEM;
  }
  const hal_dacless_config_t normalized =
      public_config(context->driver->getConfig());
  context->block_size = normalized.block_size;
  context->silence_sample = (uint16_t)((1u << normalized.pwm_bits) / 2u);
  context->max_sample = (uint16_t)((1u << normalized.pwm_bits) - 1u);
  status = apply_callbacks(context);
  if (status != HAL_OK) {
    context->driver->~DAClessAudio();
    hal_mutex_destroy(context->control_mutex);
    memset(context, 0, sizeof(*context));
    pool_unlock();
    return status;
  }

  void *handle = NULL;
  status = jh_handle_allocate(&s_handle_pool, context, &handle);
  if (status != HAL_OK) {
    context->driver->~DAClessAudio();
    hal_mutex_destroy(context->control_mutex);
    memset(context, 0, sizeof(*context));
    pool_unlock();
    return status;
  }
  context->handle = reinterpret_cast<hal_dacless_t>(handle);
  *out_audio = context->handle;
  pool_unlock();
  return HAL_OK;
}

hal_status_t hal_dacless_begin(hal_dacless_t audio) {
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_dacless_context_t *context = operation.context;
  control_lock(context);
  HAL_ATOMIC_STORE(&context->completed_output_buffer, nullptr,
                   HAL_ATOMIC_RELEASE);
  const bool wants_dma = context->driver->getConfig().useDma;
  if (wants_dma && !hal_dma_pwm_audio_supported()) {
    status = HAL_EUNSUPPORTED;
  } else {
    status = context->driver->beginEx();
  }
  if (status == HAL_OK) {
    context->callbacks_frozen = true;
  }
  HAL_ATOMIC_STORE(&context->started, status == HAL_OK, HAL_ATOMIC_RELEASE);
  control_unlock(context);
  return finish_operation(&operation, status);
}

hal_status_t hal_dacless_destroy(hal_dacless_t audio) {
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  control_lock(operation.context);
  status = operation.context->driver->shutdownEx();
  if (status == HAL_OK) {
    HAL_ATOMIC_STORE(&operation.context->started, false, HAL_ATOMIC_RELEASE);
  }
  control_unlock(operation.context);
  status = finish_operation(&operation, status);
  if (status != HAL_OK) {
    return status;
  }

  status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *released = NULL;
  status = jh_handle_begin_close(&s_handle_pool, audio, &released);
  pool_unlock();
  if (status != HAL_OK) {
    return HAL_EINVAL;
  }
  finalize_context(released);
  return HAL_OK;
}

hal_status_t hal_dacless_service(hal_dacless_t audio) {
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  control_lock(operation.context);
  if (!context_started(operation.context)) {
    status = HAL_ESTATE;
  } else {
    operation.context->driver->service();
  }
  control_unlock(operation.context);
  return finish_operation(&operation, status);
}

hal_status_t hal_dacless_mute(hal_dacless_t audio) {
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  control_lock(operation.context);
  if (!context_started(operation.context)) {
    status = HAL_ESTATE;
  } else {
    status = operation.context->driver->muteEx();
  }
  control_unlock(operation.context);
  return finish_operation(&operation, status);
}

hal_status_t hal_dacless_unmute(hal_dacless_t audio) {
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  control_lock(operation.context);
  if (!context_started(operation.context)) {
    status = HAL_ESTATE;
  } else {
    status = operation.context->driver->unmuteEx();
  }
  control_unlock(operation.context);
  return finish_operation(&operation, status);
}

hal_status_t
hal_dacless_set_sample_callback(hal_dacless_t audio,
                                hal_dacless_sample_callback_t callback,
                                void *context) {
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  control_lock(operation.context);
  if (operation.context->callbacks_frozen) {
    status = HAL_ESTATE;
  } else {
    operation.context->sample_callback = callback;
    operation.context->sample_context = context;
    status = apply_callbacks(operation.context);
  }
  control_unlock(operation.context);
  return finish_operation(&operation, status);
}

hal_status_t hal_dacless_set_block_callback(
    hal_dacless_t audio, hal_dacless_block_callback_t callback, void *context) {
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  control_lock(operation.context);
  if (operation.context->callbacks_frozen) {
    status = HAL_ESTATE;
  } else {
    operation.context->block_callback = callback;
    operation.context->block_context = context;
    status = apply_callbacks(operation.context);
  }
  control_unlock(operation.context);
  return finish_operation(&operation, status);
}

hal_status_t hal_dacless_get_adc(hal_dacless_t audio, uint8_t channel,
                                 uint16_t *out_value) {
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  *out_value = 0u;
  if (hal_in_isr()) {
    return get_adc_from_isr(audio, channel, out_value);
  }
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status != HAL_OK) {
    return status;
  }
  if (channel >= operation.context->driver->getConfig().nAdcInputs) {
    status = HAL_EINVAL;
  } else {
    *out_value = operation.context->driver->getADC(channel);
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_dacless_get_sample_rate(hal_dacless_t audio, float *out_hz) {
  if (out_hz == NULL) {
    return HAL_EINVAL;
  }
  *out_hz = 0.0f;
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status == HAL_OK) {
    *out_hz = operation.context->driver->getSampleRate();
    status = finish_operation(&operation, HAL_OK);
  }
  return status;
}

hal_status_t hal_dacless_get_config(hal_dacless_t audio,
                                    hal_dacless_config_t *out_config) {
  if (out_config == NULL) {
    return HAL_EINVAL;
  }
  memset(out_config, 0, sizeof(*out_config));
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status == HAL_OK) {
    *out_config = public_config(operation.context->driver->getConfig());
    status = finish_operation(&operation, HAL_OK);
  }
  return status;
}

hal_status_t
hal_dacless_get_output_buffer(hal_dacless_t audio,
                              const volatile uint16_t **out_buffer) {
  if (out_buffer == NULL) {
    return HAL_EINVAL;
  }
  *out_buffer = NULL;
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status == HAL_OK) {
    *out_buffer = HAL_ATOMIC_LOAD(&operation.context->completed_output_buffer,
                                  HAL_ATOMIC_ACQUIRE);
    status = finish_operation(&operation, HAL_OK);
  }
  return status;
}

hal_status_t hal_dacless_get_adc_buffer(hal_dacless_t audio,
                                        const volatile uint16_t **out_buffer,
                                        uint8_t *out_count) {
  if (out_buffer == NULL) {
    return HAL_EINVAL;
  }
  *out_buffer = NULL;
  if (out_count != NULL) {
    *out_count = 0u;
  }
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status == HAL_OK) {
    *out_buffer = operation.context->driver->getAdcBuffer();
    if (out_count != NULL) {
      *out_count = operation.context->driver->getConfig().nAdcInputs;
    }
    status = finish_operation(&operation, HAL_OK);
  }
  return status;
}

hal_status_t hal_dacless_get_state(hal_dacless_t audio,
                                   hal_dacless_state_t *out_state) {
  if (out_state == NULL) {
    return HAL_EINVAL;
  }
  memset(out_state, 0, sizeof(*out_state));
  jh_dacless_operation_t operation = {};
  hal_status_t status = begin_operation(audio, &operation);
  if (status == HAL_OK) {
    control_lock(operation.context);
    out_state->started = operation.context->driver->isBegun();
    out_state->muted = operation.context->driver->isMuted();
    out_state->running = operation.context->driver->isRunning();
    out_state->dma_active = operation.context->driver->isDmaActive();
    control_unlock(operation.context);
    status = finish_operation(&operation, HAL_OK);
  }
  return status;
}

uint16_t hal_dacless_interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled) {
  return hal_dma_interpolate(x, y, mu_scaled);
}

#endif /* HAL_ENABLE_DACLESS */

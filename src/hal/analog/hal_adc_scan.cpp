#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_ADC_SCAN
#include "hal/analog/hal_adc.h"
#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"

#include <stddef.h>

namespace {

hal_mutex_t s_mutex = NULL;
bool s_running = false;
hal_adc_scan_config_t s_config = {};
uint8_t s_positions[HAL_ADC_SCAN_MAX_PINS] = {};
uint32_t s_frame_period_ns = 0u;
uint32_t s_taken_sequence = 0u;

bool ensure_mutex(void) {
  return jh_hal_mutex_try_create_once(&s_mutex) != NULL;
}

hal_status_t validate(const hal_adc_scan_config_t *config) {
  if (config == NULL || config->buffer == NULL || config->pin_count == 0u ||
      config->pin_count > HAL_ADC_SCAN_MAX_PINS) {
    return HAL_EINVAL;
  }
  for (uint8_t i = 0u; i < config->pin_count; ++i) {
    const uint8_t pin = config->pins[i];
    if (pin != HAL_ADC_SCAN_PIN_TEMPERATURE && !hal_adc_is_pin_supported(pin)) {
      return HAL_EINVAL;
    }
    for (uint8_t j = 0u; j < i; ++j) {
      if (config->pins[j] == pin) {
        return HAL_EINVAL;
      }
    }
  }
  if (config->conversion_period_ns < HAL_ADC_SCAN_MIN_CONVERSION_NS ||
      config->conversion_period_ns > HAL_ADC_SCAN_MAX_CONVERSION_NS) {
    return HAL_EINVAL;
  }
  const uint32_t samples = config->block_frames * (uint32_t)config->pin_count;
  if (config->block_frames < HAL_ADC_SCAN_MIN_BLOCK_FRAMES ||
      samples > HAL_ADC_SCAN_MAX_BLOCK_SAMPLES ||
      ((uintptr_t)config->buffer & 3u) != 0u) {
    return HAL_EINVAL;
  }
  return HAL_OK;
}

uint8_t position_locked(uint8_t pin) {
  if (!s_running) {
    return UINT8_MAX;
  }
  for (uint8_t i = 0u; i < s_config.pin_count; ++i) {
    if (s_config.pins[i] == pin) {
      return s_positions[i];
    }
  }
  return UINT8_MAX;
}

} // namespace

hal_status_t hal_adc_scan_start(const hal_adc_scan_config_t *config) {
  const hal_status_t valid = validate(config);
  if (valid != HAL_OK) {
    return valid;
  }
  if (!ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_mutex);
  if (s_running) {
    hal_mutex_unlock(s_mutex);
    return HAL_EBUSY;
  }
  s_config = *config;
  s_taken_sequence = 0u;
  uint32_t period_ns = 0u;
  uint8_t positions[HAL_ADC_SCAN_MAX_PINS] = {};
  const hal_status_t status =
      jh_adc_scan_start(&s_config, positions, &period_ns);
  if (status != HAL_OK) {
    (void)jh_adc_scan_stop();
    hal_mutex_unlock(s_mutex);
    return status;
  }
  for (uint8_t i = 0u; i < HAL_ADC_SCAN_MAX_PINS; ++i) {
    s_positions[i] = positions[i];
  }
  s_frame_period_ns = period_ns;
  s_running = true;
  hal_mutex_unlock(s_mutex);
  return HAL_OK;
}

hal_status_t hal_adc_scan_stop(void) {
  if (!ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_mutex);
  const hal_status_t status = jh_adc_scan_stop();
  if (status == HAL_OK) {
    s_running = false;
    s_frame_period_ns = 0u;
  }
  hal_mutex_unlock(s_mutex);
  return status;
}

bool hal_adc_scan_is_running(void) {
  if (!ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_mutex);
  const bool running = s_running;
  hal_mutex_unlock(s_mutex);
  return running;
}

hal_status_t hal_adc_scan_take(hal_adc_scan_block_t *block) {
  if (block == NULL) {
    return HAL_EINVAL;
  }
  if (!ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_mutex);
  if (!s_running) {
    hal_mutex_unlock(s_mutex);
    return HAL_ESTATE;
  }
  hal_adc_scan_block_t newest = {};
  if (!jh_adc_scan_completed(&newest) || newest.sequence == s_taken_sequence) {
    hal_mutex_unlock(s_mutex);
    return HAL_EAGAIN;
  }
  s_taken_sequence = newest.sequence;
  *block = newest;
  hal_mutex_unlock(s_mutex);
  return HAL_OK;
}

hal_status_t hal_adc_scan_latest(uint8_t pin, uint16_t *raw) {
  if (raw == NULL) {
    return HAL_EINVAL;
  }
  if (!ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_mutex);
  if (!s_running) {
    hal_mutex_unlock(s_mutex);
    return HAL_ESTATE;
  }
  const uint8_t position = position_locked(pin);
  if (position == UINT8_MAX) {
    hal_mutex_unlock(s_mutex);
    return HAL_ENOENT;
  }
  const hal_status_t status = jh_adc_scan_latest(position, raw);
  hal_mutex_unlock(s_mutex);
  return status;
}

uint32_t hal_adc_scan_frame_period_ns(void) {
  if (!ensure_mutex()) {
    return 0u;
  }
  hal_mutex_lock(s_mutex);
  const uint32_t period = s_running ? s_frame_period_ns : 0u;
  hal_mutex_unlock(s_mutex);
  return period;
}

uint8_t hal_adc_scan_pin_position(uint8_t pin) {
  if (!ensure_mutex()) {
    return UINT8_MAX;
  }
  hal_mutex_lock(s_mutex);
  const uint8_t position = position_locked(pin);
  hal_mutex_unlock(s_mutex);
  return position;
}

#endif // HAL_ENABLE_ADC_SCAN

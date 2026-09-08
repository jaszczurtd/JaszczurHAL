#include "rp2040_adc_shared.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"

#include <hardware/adc.h>
#include <pico/stdlib.h>

static hal_mutex_t s_adc_mutex = NULL;
static bool s_adc_initialized = false;
static bool s_dma_owned = false;
static uint8_t s_resolution_bits = 12u;

static bool adc_ensure_mutex(void) {
  return jh_hal_mutex_try_create_once(&s_adc_mutex) != NULL;
}

static void adc_ensure_initialized(void) {
  if (!s_adc_initialized) {
    adc_init();
    s_adc_initialized = true;
  }
}

static int scale_adc_result(uint16_t raw) {
  const uint32_t max_in = (1u << 12u) - 1u;
  const uint32_t max_out = (1u << s_resolution_bits) - 1u;
  return (int)(((uint32_t)raw * max_out + (max_in / 2u)) / max_in);
}

void rp2040_adc_set_resolution(uint8_t bits) {
  if (!adc_ensure_mutex()) {
    return;
  }
  hal_mutex_lock(s_adc_mutex);
  s_resolution_bits = bits;
  hal_mutex_unlock(s_adc_mutex);
}

int rp2040_adc_read_gpio(uint8_t pin) {
  if (!adc_ensure_mutex()) {
    return 0;
  }
  hal_mutex_lock(s_adc_mutex);
  if (s_dma_owned) {
    hal_mutex_unlock(s_adc_mutex);
    return 0;
  }
  adc_ensure_initialized();
  adc_gpio_init(pin);
  adc_select_input((uint)(pin - 26u));
  const int value = scale_adc_result(adc_read());
  hal_mutex_unlock(s_adc_mutex);
  return value;
}

hal_status_t rp2040_adc_read_temperature_raw_ex(uint16_t *out_raw) {
  if (out_raw == NULL) {
    return HAL_EINVAL;
  }
  if (!adc_ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_adc_mutex);
  if (s_dma_owned) {
    hal_mutex_unlock(s_adc_mutex);
    return HAL_EBUSY;
  }
  adc_ensure_initialized();
  adc_set_temp_sensor_enabled(true);
  sleep_ms(1);
  adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
  const uint16_t raw = adc_read();
  adc_set_temp_sensor_enabled(false);
  hal_mutex_unlock(s_adc_mutex);
  *out_raw = raw;
  return HAL_OK;
}

uint16_t rp2040_adc_read_temperature_raw(void) {
  uint16_t raw = 0u;
  (void)rp2040_adc_read_temperature_raw_ex(&raw);
  return raw;
}

hal_status_t rp2040_adc_acquire_dma(void) {
  if (!adc_ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_adc_mutex);
  if (s_dma_owned) {
    hal_mutex_unlock(s_adc_mutex);
    return HAL_EBUSY;
  }
  adc_ensure_initialized();
  s_dma_owned = true;
  hal_mutex_unlock(s_adc_mutex);
  return HAL_OK;
}

void rp2040_adc_release_dma(void) {
  if (!adc_ensure_mutex()) {
    return;
  }
  hal_mutex_lock(s_adc_mutex);
  s_dma_owned = false;
  hal_mutex_unlock(s_adc_mutex);
}

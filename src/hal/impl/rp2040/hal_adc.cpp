#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_adc.h"
#include "../../hal_config.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"

#include <hardware/adc.h>
#include <hardware/gpio.h>

static hal_mutex_t s_adc_mutex = NULL;
static bool s_adc_initialized = false;
static uint8_t s_resolution_bits = 12u;

static void adc_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_adc_mutex);
}

static uint8_t clamp_resolution(uint8_t bits) {
  if (bits < 1u) {
    HAL_ASSERT(false, "hal_adc_set_resolution: resolution is below 1 bit");
    return 1u;
  }
  if (bits > 16u) {
    HAL_ASSERT(false, "hal_adc_set_resolution: resolution is above 16 bits");
    return 16u;
  }
  return bits;
}

static bool adc_pin_valid(uint8_t pin) { return pin >= 26u && pin <= 29u; }

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

void hal_adc_set_resolution(uint8_t bits) {
  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  s_resolution_bits = clamp_resolution(bits);
  hal_mutex_unlock(s_adc_mutex);
}

int hal_adc_read(uint8_t pin) {
  if (!adc_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_adc_read: unsupported ADC pin");
    return 0;
  }

  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  adc_ensure_initialized();
  adc_gpio_init(pin);
  adc_select_input((uint)(pin - 26u));
  int val = scale_adc_result(adc_read());
  hal_mutex_unlock(s_adc_mutex);
  return val;
}
#endif // HAL_TARGET_IS_RP2040

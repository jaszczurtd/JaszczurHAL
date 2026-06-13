#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_adc.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"
#include <Arduino.h>

static hal_mutex_t s_adc_mutex = NULL;

static void adc_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_adc_mutex);
}

void hal_adc_set_resolution(uint8_t bits) {
  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  analogReadResolution(bits);
  hal_mutex_unlock(s_adc_mutex);
}

int hal_adc_read(uint8_t pin) {
  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  int val = analogRead(pin);
  hal_mutex_unlock(s_adc_mutex);
  return val;
}
#endif // HAL_TARGET_IS_RP2040

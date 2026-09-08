#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_pwm_freq_internal.h"
#include "hal/gpio/hal_pwm_freq_pool.h"
#include "hal_mock.h"

#include <string.h>

struct hal_pwm_freq_channel_impl_s {
  uint8_t pin;
  uint32_t frequency_hz;
  uint32_t resolution;
  int last_value;
  int in_use;
  int running;
};

static hal_pwm_freq_channel_impl_t s_pool[HAL_PWM_FREQ_MAX_CHANNELS];

hal_status_t jh_hal_pwm_freq_try_create(uint8_t pin, uint32_t frequency_hz,
                                        uint32_t resolution,
                                        hal_pwm_freq_channel_t *out_channel) {
  const hal_status_t args_status =
      jh_hal_pwm_freq_prepare_create(frequency_hz, resolution, out_channel);
  if (args_status != HAL_OK) {
    return args_status;
  }
  hal_pwm_freq_channel_impl_t *ch = NULL;
  for (int i = 0; i < hal_get_config()->pwm_freq_max_channels; i++) {
    if (!s_pool[i].in_use) {
      ch = &s_pool[i];
      break;
    }
  }
  if (!ch) {
    return HAL_ENOMEM;
  }
  memset(ch, 0, sizeof(*ch));
  ch->in_use = 1;
  ch->pin = pin;
  ch->frequency_hz = frequency_hz;
  ch->resolution = resolution;
  ch->last_value = 0;
  ch->running = 0;
  *out_channel = ch;
  return HAL_OK;
}

hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin, uint32_t frequency_hz,
                                           uint32_t resolution) {
  return jh_hal_pwm_freq_create_compat(pin, frequency_hz, resolution);
}

uint32_t hal_pwm_freq_source_clock_hz(uint8_t pin) {
  (void)pin;
  return 125000000u;
}

void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value) {
  if (!ch) {
    return;
  }
  ch->last_value = value;
  ch->running = 1;
}

void hal_pwm_freq_stop(hal_pwm_freq_channel_t ch) {
  if (!ch) {
    return;
  }
  ch->running = 0;
}

void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch) {
  if (ch)
    ch->in_use = 0;
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

int hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch) {
  return ch ? ch->last_value : 0;
}

uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch) {
  return ch ? ch->frequency_hz : 0;
}

uint8_t hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch) {
  return ch ? ch->pin : 0;
}

bool hal_mock_pwm_freq_is_running(hal_pwm_freq_channel_t ch) {
  return ch ? (ch->running != 0) : false;
}
#endif // HAL_TARGET_IS_MOCK

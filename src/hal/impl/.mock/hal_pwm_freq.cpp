#include "../../hal_pwm_freq.h"
#include "../../hal_config.h"
#include "hal_mock.h"

#include <string.h>

struct hal_pwm_freq_channel_impl_s {
    uint8_t  pin;
    uint32_t frequency_hz;
    uint32_t resolution;
    int      last_value;
    int      in_use;
};

static hal_pwm_freq_channel_impl_t s_pool[HAL_PWM_FREQ_MAX_CHANNELS];

hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin,
                                           uint32_t frequency_hz,
                                           uint32_t resolution) {
    hal_pwm_freq_channel_impl_t *ch = NULL;
    for (int i = 0; i < hal_get_config()->pwm_freq_max_channels; i++) {
        if (!s_pool[i].in_use) { ch = &s_pool[i]; break; }
    }
    if (!ch) {
        HAL_ASSERT(0, "hal_pwm_freq: pool exhausted - increase HAL_PWM_FREQ_MAX_CHANNELS");
        return NULL;
    }
    memset(ch, 0, sizeof(*ch));
    ch->in_use       = 1;
    ch->pin          = pin;
    ch->frequency_hz = frequency_hz;
    ch->resolution   = resolution;
    ch->last_value   = 0;
    return ch;
}

void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value) {
    ch->last_value = value;
}

void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch) {
    if (ch) ch->in_use = 0;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

int hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch) {
    return ch ? ch->last_value : 0;
}

uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch) {
    return ch ? ch->frequency_hz : 0;
}

uint8_t hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch) {
    return ch ? ch->pin : 0;
}

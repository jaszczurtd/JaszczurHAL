#include "../../hal_config.h"
#ifndef HAL_DISABLE_PWM_FREQ

#include "../../hal_pwm_freq.h"
#include "../../hal_sync.h"
#include "../../hal_serial.h"

#include <string.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>

static hal_mutex_t pwm_mutex = NULL;

static void pwm_ensure_mutex(void) {
    if (!pwm_mutex) {
        hal_critical_section_enter();
        if (!pwm_mutex) {
            pwm_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

struct hal_pwm_freq_channel_impl_s {
    uint32_t pin;
    uint32_t frequency_hz;
    uint32_t analogScale;       // effective wrap value after scaling
    uint16_t pseudoScale;       // left-shift applied before write  (>255 clkdiv workaround)
    uint16_t slowScale;         // right-shift applied before write (<1   clkdiv workaround)
    int      in_use;
    int      started;           // deferred start: 0 until first write
};

static hal_pwm_freq_channel_impl_t s_pool[HAL_PWM_FREQ_MAX_CHANNELS];

hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin,
                                           uint32_t frequency_hz,
                                           uint32_t resolution) {
    pwm_ensure_mutex();

    hal_pwm_freq_channel_impl_t *cfg = NULL;
    for (int i = 0; i < hal_get_config()->pwm_freq_max_channels; i++) {
        if (!s_pool[i].in_use) { cfg = &s_pool[i]; break; }
    }
    HAL_ASSERT(cfg != NULL, "hal_pwm_freq: pool exhausted - increase HAL_PWM_FREQ_MAX_CHANNELS");
    if (!cfg) return NULL;

    memset(cfg, 0, sizeof(*cfg));
    cfg->in_use       = 1;
    cfg->pin          = pin;
    cfg->frequency_hz = frequency_hz;
    cfg->analogScale  = resolution;
    cfg->pseudoScale  = 1;
    cfg->slowScale    = 1;

    // Scale up until clkdiv fits in [1, 255]
    while (((clock_get_hz(clk_sys) / ((float)cfg->analogScale * cfg->frequency_hz)) > 255.0f) &&
           (cfg->analogScale < 32768u)) {
        cfg->pseudoScale++;
        cfg->analogScale *= 2u;
    }

    // Scale down if clkdiv would be < 1
    while (((clock_get_hz(clk_sys) / ((float)cfg->analogScale * cfg->frequency_hz)) < 1.0f) &&
           (cfg->analogScale >= 6u)) {
        cfg->slowScale++;
        cfg->analogScale /= 2u;
    }

    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv(&c, clock_get_hz(clk_sys) /
                                  ((float)cfg->analogScale * cfg->frequency_hz));
    pwm_config_set_wrap(&c, cfg->analogScale - 1u);

    // Configure slice but do NOT start yet — prevents glitch on pins
    // with inverted logic (0% duty = actuator ON).  The slice and GPIO
    // function are enabled on the first hal_pwm_freq_write() call,
    // after the correct duty-cycle level has been set.
    pwm_init(pwm_gpio_to_slice_num(cfg->pin), &c, false);
    cfg->started = 0;

    return cfg;
}

void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value) {
    if (!ch) {
        hal_derr_limited("pwm_freq", "write called with NULL channel");
        return;
    }
    pwm_ensure_mutex();

    hal_pwm_freq_channel_impl_t *cfg = ch;

    value <<= cfg->pseudoScale;
    value >>= cfg->slowScale;

    if (value < 0) {
        value = 0;
    } else if ((uint32_t)value > cfg->analogScale) {
        value = (int)cfg->analogScale;
    }

    hal_mutex_lock(pwm_mutex);
    pwm_set_gpio_level(cfg->pin, (uint16_t)value);
    if (!cfg->started) {
        gpio_set_function(cfg->pin, GPIO_FUNC_PWM);
        pwm_set_enabled(pwm_gpio_to_slice_num(cfg->pin), true);
        cfg->started = 1;
    }
    hal_mutex_unlock(pwm_mutex);
}

void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch) {
    if (ch) ch->in_use = 0;
}


#endif /* HAL_DISABLE_PWM_FREQ */

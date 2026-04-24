#if !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32)

#include "../../hal_pwm.h"

static uint8_t s_resolution = 8u;
static uint32_t s_values[128] = {};

void hal_pwm_set_resolution(uint8_t bits) {
    s_resolution = bits;
    (void)s_resolution;
}

void hal_pwm_write(uint8_t pin, uint32_t value) {
    if (pin < 128u) {
        s_values[pin] = value;
    }
}

#endif /* !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32) */

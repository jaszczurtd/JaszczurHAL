#if !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32)

#include "../../hal_gpio.h"

static bool s_state[128] = {};
static hal_gpio_mode_t s_mode[128] = {};
static void (*s_callback[128])(void) = {};
static hal_gpio_irq_mode_t s_irq_mode[128] = {};
static hal_irq_priority_t s_gpio_irq_priority = HAL_IRQ_PRIORITY_DEFAULT;

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
    if (pin < 128u) {
        s_mode[pin] = mode;
    }
}

void hal_gpio_write(uint8_t pin, bool high) {
    if (pin < 128u) {
        s_state[pin] = high;
    }
}

bool hal_gpio_read(uint8_t pin) {
    return (pin < 128u) ? s_state[pin] : false;
}

void hal_gpio_attach_interrupt(uint8_t pin,
                               void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
    if (pin < 128u) {
        s_callback[pin] = callback;
        s_irq_mode[pin] = mode;
    }
}

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
    s_gpio_irq_priority = priority;
}

#endif /* !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32) */

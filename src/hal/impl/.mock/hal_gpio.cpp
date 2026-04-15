#include "../../hal_gpio.h"
#include "hal_mock.h"

static bool           s_state[64] = {};
static hal_gpio_mode_t s_mode[64]  = {};
static void          (*s_callback[64])(void) = {};
static hal_gpio_irq_mode_t s_irq_mode[64] = {};

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
    if (pin < 64) s_mode[pin] = mode;
}

void hal_gpio_write(uint8_t pin, bool high) {
    if (pin < 64) s_state[pin] = high;
}

bool hal_gpio_read(uint8_t pin) {
    return (pin < 64) ? s_state[pin] : false;
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void), hal_gpio_irq_mode_t mode) {
    if (pin < 64) {
        s_callback[pin] = callback;
        s_irq_mode[pin] = mode;
    }
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

bool hal_mock_gpio_get_state(uint8_t pin) {
    return (pin < 64) ? s_state[pin] : false;
}

bool hal_mock_gpio_is_output(uint8_t pin) {
    return (pin < 64) ? (s_mode[pin] == HAL_GPIO_OUTPUT) : false;
}

hal_gpio_mode_t hal_mock_gpio_get_mode(uint8_t pin) {
    return (pin < 64) ? s_mode[pin] : HAL_GPIO_INPUT;
}

void hal_mock_gpio_inject_level(uint8_t pin, bool high) {
    if (pin < 64) s_state[pin] = high;
}

void hal_mock_gpio_fire_interrupt(uint8_t pin) {
    if (pin < 64 && s_callback[pin]) {
        s_callback[pin]();
    }
}

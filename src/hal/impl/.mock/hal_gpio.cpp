#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_gpio.h"
#include "hal_mock.h"

#include <stddef.h>
#include <string.h>

#define MOCK_GPIO_MAX_PINS 64u
#define MOCK_GPIO_READ_SCRIPT_MAX 128u

static bool           s_state[64] = {};
static hal_gpio_mode_t s_mode[64]  = {};
static void          (*s_callback[64])(void) = {};
static hal_gpio_irq_mode_t s_irq_mode[64] = {};
static bool           s_read_script[MOCK_GPIO_MAX_PINS][MOCK_GPIO_READ_SCRIPT_MAX] = {};
static size_t         s_read_script_len[MOCK_GPIO_MAX_PINS] = {};
static size_t         s_read_script_pos[MOCK_GPIO_MAX_PINS] = {};

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
    if (pin < 64) s_mode[pin] = mode;
}

void hal_gpio_write(uint8_t pin, bool high) {
    if (pin < 64) s_state[pin] = high;
}

bool hal_gpio_read(uint8_t pin) {
    if (pin >= MOCK_GPIO_MAX_PINS) {
        return false;
    }
    if (s_read_script_pos[pin] < s_read_script_len[pin]) {
        return s_read_script[pin][s_read_script_pos[pin]++];
    }
    return s_state[pin];
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void), hal_gpio_irq_mode_t mode) {
    if (pin < 64) {
        s_callback[pin] = callback;
        s_irq_mode[pin] = mode;
    }
}

static hal_irq_priority_t s_gpio_irq_priority = HAL_IRQ_PRIORITY_DEFAULT;

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
    s_gpio_irq_priority = priority;
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

void hal_mock_gpio_push_read_sequence(uint8_t pin, const bool *levels, size_t len) {
    if (pin >= MOCK_GPIO_MAX_PINS) {
        return;
    }
    s_read_script_len[pin] = 0u;
    s_read_script_pos[pin] = 0u;
    if (levels == NULL) {
        return;
    }
    if (len > MOCK_GPIO_READ_SCRIPT_MAX) {
        len = MOCK_GPIO_READ_SCRIPT_MAX;
    }
    memcpy(s_read_script[pin], levels, len * sizeof(levels[0]));
    s_read_script_len[pin] = len;
}

void hal_mock_gpio_clear_read_sequence(uint8_t pin) {
    if (pin < MOCK_GPIO_MAX_PINS) {
        s_read_script_len[pin] = 0u;
        s_read_script_pos[pin] = 0u;
    }
}

void hal_mock_gpio_fire_interrupt(uint8_t pin) {
    if (pin < 64 && s_callback[pin]) {
        s_callback[pin]();
    }
}

hal_irq_priority_t hal_mock_gpio_get_irq_priority(void) {
    return s_gpio_irq_priority;
}
#endif  // HAL_TARGET_IS_MOCK

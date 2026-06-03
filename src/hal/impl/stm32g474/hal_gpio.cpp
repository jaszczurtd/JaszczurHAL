#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_gpio.h"

#ifdef JH_STM32G474_HW
/* ─────────────────────────────────────────────────────────────────────────
 * Real STM32G474 GPIO backend.
 *
 * Pin-numbering convention (the design decision the stub used to hide):
 *   pin id = port_index * 16 + pin_number
 *   port_index: A=0, B=1, C=2, D=3, E=4, F=5, G=6
 *   e.g. PA5 = 0*16 + 5 = 5 ; PB0 = 1*16 + 0 = 16 ; PC13 = 2*16 + 13 = 45
 *
 * Only GPIO direction + digital read/write are implemented for the first
 * bring-up. EXTI-based interrupts are a deliberate next step.
 * ───────────────────────────────────────────────────────────────────────── */
#include "port/stm32g474_regs.h"

static inline uint32_t pin_port(uint8_t pin) { return (uint32_t)(pin >> 4); }
static inline uint32_t pin_num(uint8_t pin)  { return (uint32_t)(pin & 0x0Fu); }

static void enable_port_clock(uint32_t port)
{
    RCC_AHB2ENR |= (1u << port);   /* GPIOAEN..GPIOGEN are bits 0..6 */
}

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode)
{
    const uint32_t port = pin_port(pin);
    const uint32_t n    = pin_num(pin);
    if (port > 6u) {
        return;
    }
    enable_port_clock(port);

    uint32_t moder_field = GPIO_MODE_INPUT;
    uint32_t pupd_field  = GPIO_PUPD_NONE;
    switch (mode) {
        case HAL_GPIO_OUTPUT:       moder_field = GPIO_MODE_OUTPUT; break;
        case HAL_GPIO_INPUT_PULLUP: pupd_field  = GPIO_PUPD_UP;     break;
        case HAL_GPIO_INPUT:
        default:                    break;
    }

    GPIO_MODER(port) = (GPIO_MODER(port) & ~(0x3u << (n * 2u))) |
                       (moder_field << (n * 2u));
    GPIO_PUPDR(port) = (GPIO_PUPDR(port) & ~(0x3u << (n * 2u))) |
                       (pupd_field << (n * 2u));
}

void hal_gpio_write(uint8_t pin, bool high)
{
    const uint32_t port = pin_port(pin);
    const uint32_t n    = pin_num(pin);
    if (port > 6u) {
        return;
    }
    /* BSRR: low half sets, high half resets (atomic, no read-modify-write). */
    GPIO_BSRR(port) = high ? (1u << n) : (1u << (n + 16u));
}

bool hal_gpio_read(uint8_t pin)
{
    const uint32_t port = pin_port(pin);
    const uint32_t n    = pin_num(pin);
    if (port > 6u) {
        return false;
    }
    return (GPIO_IDR(port) & (1u << n)) != 0u;
}

void hal_gpio_attach_interrupt(uint8_t pin,
                               void (*callback)(void),
                               hal_gpio_irq_mode_t mode)
{
    /* EXTI/NVIC wiring is the next bring-up step. */
    (void)pin;
    (void)callback;
    (void)mode;
}

void hal_gpio_set_irq_priority(hal_irq_priority_t priority)
{
    /* No EXTI yet; no-op until interrupts land. */
    (void)priority;
}

#else /* !JH_STM32G474_HW : host-stub backend (RAM-array state for unit tests) */

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

#endif /* JH_STM32G474_HW */

#endif  // HAL_TARGET_IS_STM32G474

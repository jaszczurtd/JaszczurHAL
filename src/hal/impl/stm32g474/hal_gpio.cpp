#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/gpio/hal_gpio.h"
#include "hal/gpio/hal_gpio_common.h"

#ifdef JH_STM32G474_HW
/* ─────────────────────────────────────────────────────────────────────────
 * Real STM32G474 GPIO backend.
 *
 * Pin-numbering convention (the design decision the stub used to hide):
 *   pin id = port_index * 16 + pin_number
 *   port_index: A=0, B=1, C=2, D=3, E=4, F=5, G=6
 *   e.g. PA5 = 0*16 + 5 = 5 ; PB0 = 1*16 + 0 = 16 ; PC13 = 2*16 + 13 = 45
 *
 * GPIO direction + digital read/write + EXTI interrupts are implemented.
 * STM32 EXTI routes one callback per line (0..15), selected by pin number.
 * ───────────────────────────────────────────────────────────────────────── */
#include "port/stm32g474_regs.h"

static inline uint32_t pin_port(uint8_t pin) { return (uint32_t)(pin >> 4); }
static inline uint32_t pin_num(uint8_t pin) { return (uint32_t)(pin & 0x0Fu); }

static void (*s_exti_callback[16])(void) = {};
static bool s_exti_attached[16] = {};
static uint8_t s_exti_owner[16] = {};
static uint8_t s_exti_pin[16] = {};
static hal_irq_priority_t s_gpio_irq_priority = HAL_IRQ_PRIORITY_DEFAULT;

static inline uint32_t exti_line_mask(uint32_t line) { return 1u << line; }

static bool gpio_pin_valid(uint8_t pin) { return pin_port(pin) <= 6u; }

static inline uint8_t nvic_prio_from_hal(hal_irq_priority_t priority) {
  switch (priority) {
  case HAL_IRQ_PRIORITY_HIGHEST:
    return 0x00u;
  case HAL_IRQ_PRIORITY_HIGH:
    return 0x40u;
  case HAL_IRQ_PRIORITY_LOW:
    return 0xC0u;
  case HAL_IRQ_PRIORITY_DEFAULT:
  default:
    return 0x80u;
  }
}

static inline uint32_t exti_irqn_for_line(uint32_t line) {
  if (line <= 4u) {
    return EXTI0_IRQn + line;
  }
  return (line <= 9u) ? EXTI9_5_IRQn : EXTI15_10_IRQn;
}

static void exti_clear_pending(uint32_t mask) { EXTI_PR1 = mask; }

static void exti_apply_priority(void) {
  const uint8_t hw_prio = nvic_prio_from_hal(s_gpio_irq_priority);
  NVIC_IPR8(EXTI0_IRQn) = hw_prio;
  NVIC_IPR8(EXTI1_IRQn) = hw_prio;
  NVIC_IPR8(EXTI2_IRQn) = hw_prio;
  NVIC_IPR8(EXTI3_IRQn) = hw_prio;
  NVIC_IPR8(EXTI4_IRQn) = hw_prio;
  NVIC_IPR8(EXTI9_5_IRQn) = hw_prio;
  NVIC_IPR8(EXTI15_10_IRQn) = hw_prio;
}

static void exti_enable_irq(uint32_t irqn) {
  const uint32_t bank = irqn >> 5u;
  const uint32_t bit = 1u << (irqn & 31u);
  NVIC_ICPR(bank) = bit;
  NVIC_ISER(bank) = bit;
}

static void exti_dispatch_line(uint32_t line) {
  const uint32_t mask = exti_line_mask(line);
  if ((EXTI_PR1 & mask) == 0u) {
    return;
  }

  exti_clear_pending(mask);
  void (*callback)(void) = s_exti_callback[line];
  if (callback != nullptr) {
    callback();
  }
}

static void exti_dispatch_range(uint32_t first, uint32_t last) {
  for (uint32_t line = first; line <= last; ++line) {
    exti_dispatch_line(line);
  }
}

static void enable_port_clock(uint32_t port) {
  RCC_AHB2ENR |= (1u << port); /* GPIOAEN..GPIOGEN are bits 0..6 */
}

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  const uint32_t port = pin_port(pin);
  const uint32_t n = pin_num(pin);
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid pin");
    return;
  }
  if (!jh_hal_gpio_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid mode");
    return;
  }
  enable_port_clock(port);

  uint32_t moder_field = GPIO_MODE_INPUT;
  uint32_t pupd_field = GPIO_PUPD_NONE;
  bool open_drain = false;
  switch (mode) {
  case HAL_GPIO_OUTPUT:
  case HAL_GPIO_OUTPUT_LOW:
    moder_field = GPIO_MODE_OUTPUT;
    GPIO_BSRR(port) = 1u << (n + 16u);
    break;
  case HAL_GPIO_OUTPUT_HIGH:
    moder_field = GPIO_MODE_OUTPUT;
    GPIO_BSRR(port) = 1u << n;
    break;
  case HAL_GPIO_INPUT_PULLUP:
    pupd_field = GPIO_PUPD_UP;
    break;
  case HAL_GPIO_INPUT_PULLDOWN:
    pupd_field = GPIO_PUPD_DOWN;
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN:
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH:
    moder_field = GPIO_MODE_OUTPUT;
    open_drain = true;
    GPIO_BSRR(port) = 1u << n;
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW:
    moder_field = GPIO_MODE_OUTPUT;
    open_drain = true;
    GPIO_BSRR(port) = 1u << (n + 16u);
    break;
  case HAL_GPIO_INPUT:
  default:
    break;
  }

  if (open_drain) {
    GPIO_OTYPER(port) |= (1u << n);
  } else {
    GPIO_OTYPER(port) &= ~(1u << n);
  }
  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (moder_field << (n * 2u));
  GPIO_PUPDR(port) =
      (GPIO_PUPDR(port) & ~(0x3u << (n * 2u))) | (pupd_field << (n * 2u));
}

void hal_gpio_write(uint8_t pin, bool high) {
  const uint32_t port = pin_port(pin);
  const uint32_t n = pin_num(pin);
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_write: invalid pin");
    return;
  }
  /* BSRR: low half sets, high half resets (atomic, no read-modify-write). */
  GPIO_BSRR(port) = high ? (1u << n) : (1u << (n + 16u));
}

bool hal_gpio_read(uint8_t pin) {
  const uint32_t port = pin_port(pin);
  const uint32_t n = pin_num(pin);
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_read: invalid pin");
    return false;
  }
  return (GPIO_IDR(port) & (1u << n)) != 0u;
}

hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin, void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core) {
  const uint32_t port = pin_port(pin);
  const uint32_t line = pin_num(pin);
  if (!gpio_pin_valid(pin) || line > 15u) {
    return HAL_EINVAL;
  }
  if (callback == nullptr) {
    return HAL_EINVAL;
  }
  if (!jh_hal_gpio_irq_mode_valid(mode)) {
    return HAL_EINVAL;
  }
  if (owner_core > 1u) {
    return HAL_EINVAL;
  }
  if (owner_core != 0u) {
    return HAL_ESTATE;
  }
  if (s_exti_attached[line] && s_exti_owner[line] != owner_core) {
    return HAL_ESTATE;
  }

  const uint32_t mask = exti_line_mask(line);
  enable_port_clock(port);
  RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;

  const uint32_t exticr_idx = line >> 2u;
  const uint32_t exticr_shift = (line & 0x3u) * 4u;
  SYSCFG_EXTICR(exticr_idx) =
      (SYSCFG_EXTICR(exticr_idx) & ~(0xFu << exticr_shift)) |
      (port << exticr_shift);

  EXTI_IMR1 &= ~mask;
  EXTI_RTSR1 &= ~mask;
  EXTI_FTSR1 &= ~mask;
  exti_clear_pending(mask);

  s_exti_callback[line] = callback;
  switch (mode) {
  case HAL_GPIO_IRQ_FALLING:
    EXTI_FTSR1 |= mask;
    break;
  case HAL_GPIO_IRQ_RISING:
    EXTI_RTSR1 |= mask;
    break;
  case HAL_GPIO_IRQ_CHANGE:
  default:
    EXTI_RTSR1 |= mask;
    EXTI_FTSR1 |= mask;
    break;
  }

  EXTI_IMR1 |= mask;
  exti_apply_priority();
  exti_enable_irq(exti_irqn_for_line(line));
  s_exti_pin[line] = pin;
  s_exti_owner[line] = owner_core;
  s_exti_attached[line] = true;
  return HAL_OK;
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
  const hal_status_t status =
      hal_gpio_attach_interrupt_ex(pin, callback, mode, 0u);
  HAL_ASSERT(status == HAL_OK, "hal_gpio_attach_interrupt: attach failed");
}

hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin) {
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  const uint32_t line = pin_num(pin);
  if (line > 15u) {
    return HAL_EINVAL;
  }
  if (!s_exti_attached[line] || s_exti_pin[line] != pin) {
    return HAL_ENOENT;
  }
  if (s_exti_owner[line] != 0u) {
    return HAL_ESTATE;
  }

  const uint32_t mask = exti_line_mask(line);
  EXTI_IMR1 &= ~mask;
  EXTI_RTSR1 &= ~mask;
  EXTI_FTSR1 &= ~mask;
  exti_clear_pending(mask);
  s_exti_callback[line] = nullptr;
  s_exti_attached[line] = false;
  s_exti_owner[line] = HAL_GPIO_IRQ_CORE_NONE;
  s_exti_pin[line] = 0u;
  return HAL_OK;
}

void hal_gpio_detach_interrupt(uint8_t pin) {
  const hal_status_t status = hal_gpio_detach_interrupt_ex(pin);
  HAL_ASSERT(status == HAL_OK || status == HAL_ENOENT,
             "hal_gpio_detach_interrupt: detach failed");
}

hal_status_t hal_gpio_get_interrupt_owner_ex(uint8_t pin,
                                             uint8_t *out_owner_core) {
  if (out_owner_core == nullptr) {
    return HAL_EINVAL;
  }
  *out_owner_core = HAL_GPIO_IRQ_CORE_NONE;
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  const uint32_t line = pin_num(pin);
  if (!s_exti_attached[line] || s_exti_pin[line] != pin) {
    return HAL_ENOENT;
  }
  *out_owner_core = s_exti_owner[line];
  return HAL_OK;
}

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
  if (priority > HAL_IRQ_PRIORITY_LOW) {
    priority = HAL_IRQ_PRIORITY_DEFAULT;
  }
  s_gpio_irq_priority = priority;
  exti_apply_priority();
}

extern "C" void EXTI0_IRQHandler(void) { exti_dispatch_line(0u); }
extern "C" void EXTI1_IRQHandler(void) { exti_dispatch_line(1u); }
extern "C" void EXTI2_IRQHandler(void) { exti_dispatch_line(2u); }
extern "C" void EXTI3_IRQHandler(void) { exti_dispatch_line(3u); }
extern "C" void EXTI4_IRQHandler(void) { exti_dispatch_line(4u); }
extern "C" void EXTI9_5_IRQHandler(void) { exti_dispatch_range(5u, 9u); }
extern "C" void EXTI15_10_IRQHandler(void) { exti_dispatch_range(10u, 15u); }

#else /* !JH_STM32G474_HW : host-stub backend (RAM-array state for unit tests) \
       */

static bool s_state[128] = {};
static hal_gpio_mode_t s_mode[128] = {};
static void (*s_callback[16])(void) = {};
static hal_gpio_irq_mode_t s_irq_mode[16] = {};
static bool s_irq_attached[16] = {};
static uint8_t s_irq_owner[16] = {};
static uint8_t s_irq_pin[16] = {};
static hal_irq_priority_t s_gpio_irq_priority = HAL_IRQ_PRIORITY_DEFAULT;

static bool gpio_pin_valid(uint8_t pin) { return pin < 128u; }
static uint8_t pin_num(uint8_t pin) { return (uint8_t)(pin & 0x0Fu); }

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  (void)jh_hal_gpio_store_mode(pin, mode, s_state, s_mode, gpio_pin_valid);
}

void hal_gpio_write(uint8_t pin, bool high) {
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_write: invalid pin");
    return;
  }
  s_state[pin] = high;
}

bool hal_gpio_read(uint8_t pin) {
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_read: invalid pin");
    return false;
  }
  return s_state[pin];
}

hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin, void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core) {
  const uint8_t line = pin_num(pin);
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (callback == nullptr) {
    return HAL_EINVAL;
  }
  if (!jh_hal_gpio_irq_mode_valid(mode)) {
    return HAL_EINVAL;
  }
  if (owner_core > 1u) {
    return HAL_EINVAL;
  }
  if (owner_core != 0u) {
    return HAL_ESTATE;
  }
  if (s_irq_attached[line] && s_irq_owner[line] != owner_core) {
    return HAL_ESTATE;
  }
  s_callback[line] = callback;
  s_irq_mode[line] = mode;
  s_irq_pin[line] = pin;
  s_irq_owner[line] = owner_core;
  s_irq_attached[line] = true;
  return HAL_OK;
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
  const hal_status_t status =
      hal_gpio_attach_interrupt_ex(pin, callback, mode, 0u);
  HAL_ASSERT(status == HAL_OK, "hal_gpio_attach_interrupt: attach failed");
}

hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin) {
  const uint8_t line = pin_num(pin);
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (!s_irq_attached[line] || s_irq_pin[line] != pin) {
    return HAL_ENOENT;
  }
  if (s_irq_owner[line] != 0u) {
    return HAL_ESTATE;
  }
  s_callback[line] = nullptr;
  s_irq_attached[line] = false;
  s_irq_owner[line] = HAL_GPIO_IRQ_CORE_NONE;
  s_irq_pin[line] = 0u;
  return HAL_OK;
}

void hal_gpio_detach_interrupt(uint8_t pin) {
  const hal_status_t status = hal_gpio_detach_interrupt_ex(pin);
  HAL_ASSERT(status == HAL_OK || status == HAL_ENOENT,
             "hal_gpio_detach_interrupt: detach failed");
}

hal_status_t hal_gpio_get_interrupt_owner_ex(uint8_t pin,
                                             uint8_t *out_owner_core) {
  const uint8_t line = pin_num(pin);
  if (out_owner_core == nullptr) {
    return HAL_EINVAL;
  }
  *out_owner_core = HAL_GPIO_IRQ_CORE_NONE;
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (!s_irq_attached[line] || s_irq_pin[line] != pin) {
    return HAL_ENOENT;
  }
  *out_owner_core = s_irq_owner[line];
  return HAL_OK;
}

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
  s_gpio_irq_priority = priority;
}

#endif /* JH_STM32G474_HW */

#endif // HAL_TARGET_IS_STM32G474

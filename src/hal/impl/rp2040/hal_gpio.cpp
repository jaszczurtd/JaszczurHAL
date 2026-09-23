#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/gpio/hal_gpio.h"
#include "hal/gpio/hal_gpio_common.h"
#if HAL_BOARD_HAS_CYW43 && defined(HAL_LED_BUILTIN)
#include "drivers/rp2040/rp2040_cyw43.h"
#endif
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <pico/mutex.h>
#include <pico/platform.h>

/* Bank0 pins are the only ones these tables ever index: the CYW43 pin is
 * handled before any of them is touched. */
static bool s_open_drain_mode[NUM_BANK0_GPIOS] = {};
static jh_gpio_irq_slot_t s_gpio_irq_slot[NUM_BANK0_GPIOS] = {};
/* 0 = detached, 1 = core 0, 2 = core 1. The shared mutex prevents two cores
 * from claiming an unowned pin concurrently. */
static uint8_t s_gpio_irq_owner_state[NUM_BANK0_GPIOS] = {};
auto_init_mutex(s_gpio_irq_owner_mutex);

static bool rp2040_pin_valid(uint8_t pin) { return pin < NUM_BANK0_GPIOS; }

static bool rp2040_cyw43_pin_valid(uint8_t pin) {
#if HAL_BOARD_HAS_CYW43 && defined(HAL_LED_BUILTIN)
  return pin == (uint8_t)HAL_LED_BUILTIN && pin >= 64u;
#else
  (void)pin;
  return false;
#endif
}

static bool rp2040_hal_pin_valid(uint8_t pin) {
  return rp2040_pin_valid(pin) || rp2040_cyw43_pin_valid(pin);
}

bool jh_hal_gpio_pin_valid(uint8_t pin) { return rp2040_hal_pin_valid(pin); }

static void cyw43_gpio_set_output(uint8_t pin, bool high) {
#if HAL_BOARD_HAS_CYW43 && defined(HAL_LED_BUILTIN)
  hal_cyw43_pinMode(pin, HAL_CYW43_PIN_OUTPUT);
  hal_cyw43_digitalWrite(pin, high);
#else
  (void)pin;
  (void)high;
#endif
}

static void cyw43_gpio_write(uint8_t pin, bool high) {
#if HAL_BOARD_HAS_CYW43 && defined(HAL_LED_BUILTIN)
  hal_cyw43_digitalWrite(pin, high);
#else
  (void)pin;
  (void)high;
#endif
}

static bool cyw43_gpio_read(uint8_t pin) {
#if HAL_BOARD_HAS_CYW43 && defined(HAL_LED_BUILTIN)
  return hal_cyw43_digitalRead(pin);
#else
  (void)pin;
  return false;
#endif
}

static void set_input_mode(uint8_t pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_disable_pulls(pin);
}

static void set_input_pullup_mode(uint8_t pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);
}

static void set_input_pulldown_mode(uint8_t pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_down(pin);
}

static void set_output_mode(uint8_t pin, bool high) {
  gpio_init(pin);
  gpio_put(pin, high);
  gpio_set_dir(pin, GPIO_OUT);
}

static void set_open_drain(uint8_t pin, bool high) {
  if (high) {
    gpio_set_dir(pin, GPIO_IN);
  } else {
    gpio_put(pin, false);
    gpio_set_dir(pin, GPIO_OUT);
  }
}

static uint32_t gpio_irq_events(hal_gpio_irq_mode_t mode) {
  switch (mode) {
  case HAL_GPIO_IRQ_FALLING:
    return GPIO_IRQ_EDGE_FALL;
  case HAL_GPIO_IRQ_RISING:
    return GPIO_IRQ_EDGE_RISE;
  default:
    return GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;
  }
}

static uint32_t gpio_all_irq_events(void) {
  return GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE | GPIO_IRQ_LEVEL_LOW |
         GPIO_IRQ_LEVEL_HIGH;
}

static void gpio_irq_dispatch(uint gpio, uint32_t events) {
  (void)events;
  if (gpio < NUM_BANK0_GPIOS) {
    jh_gpio_irq_slot_invoke(&s_gpio_irq_slot[gpio], (uint8_t)gpio);
  }
}

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  if (!rp2040_hal_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid pin");
    return;
  }
  if (!jh_hal_gpio_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid mode");
    return;
  }

  if (rp2040_cyw43_pin_valid(pin)) {
    switch (mode) {
    case HAL_GPIO_OUTPUT:
    case HAL_GPIO_OUTPUT_LOW:
      cyw43_gpio_set_output(pin, false);
      return;
    case HAL_GPIO_OUTPUT_HIGH:
      cyw43_gpio_set_output(pin, true);
      return;
    case HAL_GPIO_OUTPUT_OPEN_DRAIN:
    case HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW:
    case HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH:
      HAL_ASSERT(false, "hal_gpio_set_mode: open-drain unsupported on CYW43");
      return;
    case HAL_GPIO_INPUT:
    case HAL_GPIO_INPUT_PULLUP:
    case HAL_GPIO_INPUT_PULLDOWN:
    default:
      HAL_ASSERT(false, "hal_gpio_set_mode: input unsupported on CYW43");
      return;
    }
  }

  s_open_drain_mode[pin] = false;
  switch (mode) {
  case HAL_GPIO_OUTPUT:
  case HAL_GPIO_OUTPUT_LOW:
    set_output_mode(pin, false);
    break;
  case HAL_GPIO_OUTPUT_HIGH:
    set_output_mode(pin, true);
    break;
  case HAL_GPIO_INPUT_PULLUP:
    set_input_pullup_mode(pin);
    break;
  case HAL_GPIO_INPUT_PULLDOWN:
    set_input_pulldown_mode(pin);
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN:
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH:
    s_open_drain_mode[pin] = true;
    gpio_init(pin);
    gpio_put(pin, false);
    gpio_disable_pulls(pin);
    set_open_drain(pin, true);
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW:
    s_open_drain_mode[pin] = true;
    gpio_init(pin);
    gpio_put(pin, false);
    gpio_disable_pulls(pin);
    set_open_drain(pin, false);
    break;
  case HAL_GPIO_INPUT:
  default:
    set_input_mode(pin);
    break;
  }
}

void hal_gpio_write(uint8_t pin, bool high) {
  if (!rp2040_hal_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_write: invalid pin");
    return;
  }
  if (rp2040_cyw43_pin_valid(pin)) {
    cyw43_gpio_write(pin, high);
    return;
  }
  if (s_open_drain_mode[pin]) {
    set_open_drain(pin, high);
    return;
  }
  gpio_put(pin, high);
}

bool hal_gpio_read(uint8_t pin) {
  if (!rp2040_hal_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_read: invalid pin");
    return false;
  }
  if (rp2040_cyw43_pin_valid(pin)) {
    return cyw43_gpio_read(pin);
  }
  return gpio_get(pin);
}

static hal_status_t rp2040_gpio_attach(uint8_t pin,
                                       const jh_gpio_irq_slot_t *request,
                                       hal_gpio_irq_mode_t mode,
                                       uint8_t owner_core) {
  if (!rp2040_hal_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (rp2040_cyw43_pin_valid(pin)) {
    return HAL_EUNSUPPORTED;
  }
  if (!jh_gpio_irq_slot_armed(request)) {
    return HAL_EINVAL;
  }
  if (!jh_hal_gpio_irq_mode_valid(mode)) {
    return HAL_EINVAL;
  }
  if (owner_core > 1u) {
    return HAL_EINVAL;
  }

  const uint8_t current_core = (uint8_t)get_core_num();
  if (current_core != owner_core) {
    return HAL_ESTATE;
  }

  const uint8_t desired_state = (uint8_t)(owner_core + 1u);
  mutex_enter_blocking(&s_gpio_irq_owner_mutex);
  if (s_gpio_irq_owner_state[pin] != 0u &&
      s_gpio_irq_owner_state[pin] != desired_state) {
    mutex_exit(&s_gpio_irq_owner_mutex);
    return HAL_ESTATE;
  }
  s_gpio_irq_owner_state[pin] = desired_state;

  gpio_set_irq_enabled(pin, gpio_all_irq_events(), false);
  jh_gpio_irq_slot_apply(&s_gpio_irq_slot[pin], request);
  gpio_set_irq_enabled_with_callback(pin, gpio_irq_events(mode), true,
                                     gpio_irq_dispatch);
  mutex_exit(&s_gpio_irq_owner_mutex);
  return HAL_OK;
}

hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin, void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core) {
  const jh_gpio_irq_slot_t request = jh_gpio_irq_slot_make_plain(callback);
  return rp2040_gpio_attach(pin, &request, mode, owner_core);
}

hal_status_t hal_gpio_attach_interrupt_ctx_ex(uint8_t pin,
                                              hal_gpio_irq_callback_t callback,
                                              void *context,
                                              hal_gpio_irq_mode_t mode,
                                              uint8_t owner_core) {
  const jh_gpio_irq_slot_t request = jh_gpio_irq_slot_make(callback, context);
  return rp2040_gpio_attach(pin, &request, mode, owner_core);
}

hal_status_t hal_gpio_attach_interrupt_ctx(uint8_t pin,
                                           hal_gpio_irq_callback_t callback,
                                           void *context,
                                           hal_gpio_irq_mode_t mode) {
  return hal_gpio_attach_interrupt_ctx_ex(pin, callback, context, mode,
                                          (uint8_t)get_core_num());
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
  const hal_status_t status = hal_gpio_attach_interrupt_ex(
      pin, callback, mode, (uint8_t)get_core_num());
  HAL_ASSERT(status == HAL_OK, "hal_gpio_attach_interrupt: attach failed");
}

hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin) {
  if (!rp2040_hal_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (rp2040_cyw43_pin_valid(pin)) {
    return HAL_EUNSUPPORTED;
  }
  mutex_enter_blocking(&s_gpio_irq_owner_mutex);
  const uint8_t owner_state = s_gpio_irq_owner_state[pin];
  if (owner_state == 0u) {
    mutex_exit(&s_gpio_irq_owner_mutex);
    return HAL_ENOENT;
  }
  if ((uint8_t)(owner_state - 1u) != (uint8_t)get_core_num()) {
    mutex_exit(&s_gpio_irq_owner_mutex);
    return HAL_ESTATE;
  }
  gpio_set_irq_enabled(pin, gpio_all_irq_events(), false);
  jh_gpio_irq_slot_clear(&s_gpio_irq_slot[pin]);
  s_gpio_irq_owner_state[pin] = 0u;
  mutex_exit(&s_gpio_irq_owner_mutex);
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
  if (!rp2040_hal_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (rp2040_cyw43_pin_valid(pin)) {
    return HAL_EUNSUPPORTED;
  }
  mutex_enter_blocking(&s_gpio_irq_owner_mutex);
  const uint8_t owner_state = s_gpio_irq_owner_state[pin];
  if (owner_state == 0u) {
    mutex_exit(&s_gpio_irq_owner_mutex);
    return HAL_ENOENT;
  }
  *out_owner_core = (uint8_t)(owner_state - 1u);
  mutex_exit(&s_gpio_irq_owner_mutex);
  return HAL_OK;
}

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
  // RP2040 Cortex-M0+ has 4 priority levels (top 2 bits of 8-bit field):
  //   0x00 (highest), 0x40, 0x80 (default), 0xC0 (lowest).
  static const uint8_t prio_map[] = {
      [HAL_IRQ_PRIORITY_HIGHEST] = 0x00,
      [HAL_IRQ_PRIORITY_HIGH] = 0x40,
      [HAL_IRQ_PRIORITY_DEFAULT] = PICO_DEFAULT_IRQ_PRIORITY, // 0x80
      [HAL_IRQ_PRIORITY_LOW] = 0xC0,
  };
  uint8_t hw_prio = (priority <= HAL_IRQ_PRIORITY_LOW)
                        ? prio_map[priority]
                        : PICO_DEFAULT_IRQ_PRIORITY;
  irq_set_priority(IO_IRQ_BANK0, hw_prio);
}
#endif // HAL_TARGET_IS_RP

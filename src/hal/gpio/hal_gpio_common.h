#ifndef JH_HAL_GPIO_COMMON_H
#define JH_HAL_GPIO_COMMON_H

#include "hal/gpio/hal_gpio.h"

static inline bool jh_hal_gpio_mode_valid(hal_gpio_mode_t mode) {
  return mode >= HAL_GPIO_INPUT && mode <= HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
}

static inline bool jh_hal_gpio_irq_mode_valid(hal_gpio_irq_mode_t mode) {
  return mode >= HAL_GPIO_IRQ_FALLING && mode <= HAL_GPIO_IRQ_CHANGE;
}

template <typename PinValidator>
static bool jh_hal_gpio_store_mode(uint8_t pin, hal_gpio_mode_t mode,
                                   bool *state, hal_gpio_mode_t *stored_mode,
                                   PinValidator pin_valid) {
  if (!pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid pin");
    return false;
  }
  if (!jh_hal_gpio_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid mode");
    return false;
  }
  if (mode == HAL_GPIO_OUTPUT || mode == HAL_GPIO_OUTPUT_LOW ||
      mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW) {
    state[pin] = false;
  } else if (mode == HAL_GPIO_OUTPUT_HIGH ||
             mode == HAL_GPIO_OUTPUT_OPEN_DRAIN ||
             mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH) {
    state[pin] = true;
  }
  stored_mode[pin] = mode;
  return true;
}

#endif

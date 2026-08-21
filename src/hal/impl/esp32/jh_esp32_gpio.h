#pragma once

#include "hal/core/hal_config.h"

#include <driver/gpio.h>

#include <stdint.h>

#if !defined(HAL_TARGET_GPIO_VALID_MASK) ||                                    \
    !defined(HAL_TARGET_GPIO_INPUT_ONLY_MASK) ||                               \
    !defined(HAL_BOARD_GPIO_EXPOSED_MASK) ||                                   \
    !defined(HAL_BOARD_GPIO_HARD_RESERVED_MASK) ||                             \
    !defined(HAL_BOARD_GPIO_SOFT_RESERVED_MASK)
#error "JaszczurHAL: ESP32 GPIO requires generated target and board pin masks."
#endif

static inline bool jh_esp32_mask_has_pin(uint64_t mask, uint8_t pin) {
  return pin < 64u && (mask & (UINT64_C(1) << pin)) != 0u;
}

static inline bool jh_esp32_gpio_pin_valid(uint8_t pin) {
  const bool target_valid =
      jh_esp32_mask_has_pin(HAL_TARGET_GPIO_VALID_MASK, pin) &&
      GPIO_IS_VALID_GPIO((gpio_num_t)pin);
  const bool board_accessible =
      jh_esp32_mask_has_pin(HAL_BOARD_GPIO_EXPOSED_MASK, pin) ||
      jh_esp32_mask_has_pin(HAL_BOARD_GPIO_SOFT_RESERVED_MASK, pin);
  return target_valid && board_accessible &&
         !jh_esp32_mask_has_pin(HAL_BOARD_GPIO_HARD_RESERVED_MASK, pin);
}

static inline bool jh_esp32_gpio_output_pin_valid(uint8_t pin) {
  return jh_esp32_gpio_pin_valid(pin) &&
         !jh_esp32_mask_has_pin(HAL_TARGET_GPIO_INPUT_ONLY_MASK, pin) &&
         GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin);
}

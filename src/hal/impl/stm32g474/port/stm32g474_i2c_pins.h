#ifndef JH_STM32G474_I2C_PINS_H
#define JH_STM32G474_I2C_PINS_H

#include "hal/core/hal_array.h"

#include "stm32g474_regs.h"

#include <stddef.h>
#include <stdint.h>

#define JH_STM32G474_PIN(port, pin) ((uint8_t)(((port) * 16u) + (pin)))

typedef struct {
  uint8_t controller;
  bool is_sda;
  uint8_t pin;
  uint8_t af;
} jh_stm32g474_i2c_pin_af_t;

static const jh_stm32g474_i2c_pin_af_t kJhStm32g474I2cPins[] = {
    {1u, false, JH_STM32G474_PIN(1u, 8u), 4u},
    {1u, false, JH_STM32G474_PIN(0u, 13u), 4u},
    {1u, false, JH_STM32G474_PIN(0u, 15u), 4u},
    {1u, true, JH_STM32G474_PIN(1u, 7u), 4u},
    {1u, true, JH_STM32G474_PIN(1u, 9u), 4u},
    {1u, true, JH_STM32G474_PIN(0u, 14u), 4u},
    {2u, false, JH_STM32G474_PIN(0u, 9u), 4u},
    {2u, false, JH_STM32G474_PIN(2u, 4u), 4u},
    {2u, false, JH_STM32G474_PIN(5u, 6u), 4u},
    {2u, true, JH_STM32G474_PIN(0u, 8u), 4u},
    {2u, true, JH_STM32G474_PIN(5u, 0u), 4u},
};

static inline bool jh_stm32g474_i2c_find_af(uint8_t controller, bool is_sda,
                                            uint8_t pin, uint8_t *out_af) {
  for (size_t i = 0u; i < COUNTOF(kJhStm32g474I2cPins); ++i) {
    const jh_stm32g474_i2c_pin_af_t *entry = &kJhStm32g474I2cPins[i];
    if (entry->controller == controller && entry->is_sda == is_sda &&
        entry->pin == pin) {
      if (out_af != NULL) {
        *out_af = entry->af;
      }
      return true;
    }
  }
  return false;
}

static inline uint32_t jh_stm32g474_pin_port(uint8_t pin) {
  return (uint32_t)(pin >> 4u);
}

static inline uint32_t jh_stm32g474_pin_number(uint8_t pin) {
  return (uint32_t)(pin & 0x0Fu);
}

static inline void jh_stm32g474_i2c_select_hsi16(uint8_t controller) {
  if (controller == 1u) {
    RCC_CCIPR = (RCC_CCIPR & ~RCC_CCIPR_I2C1SEL_MASK) | RCC_CCIPR_I2C1SEL_HSI16;
  } else if (controller == 2u) {
    RCC_CCIPR = (RCC_CCIPR & ~RCC_CCIPR_I2C2SEL_MASK) | RCC_CCIPR_I2C2SEL_HSI16;
  }
}

static inline void jh_stm32g474_i2c_set_af_od_pullup(uint8_t pin, uint8_t af) {
  const uint32_t port = jh_stm32g474_pin_port(pin);
  const uint32_t number = jh_stm32g474_pin_number(pin);
  if (port > 6u) {
    return;
  }
  RCC_AHB2ENR |= (1u << port);
  (void)RCC_AHB2ENR;
  GPIO_MODER(port) = (GPIO_MODER(port) & ~(0x3u << (number * 2u))) |
                     (GPIO_MODE_AF << (number * 2u));
  GPIO_OTYPER(port) |= (1u << number);
  GPIO_OSPEEDR(port) |= (0x3u << (number * 2u));
  GPIO_PUPDR(port) = (GPIO_PUPDR(port) & ~(0x3u << (number * 2u))) |
                     (GPIO_PUPD_UP << (number * 2u));
  if (number < 8u) {
    GPIO_AFRL(port) = (GPIO_AFRL(port) & ~(0xFu << (number * 4u))) |
                      ((uint32_t)af << (number * 4u));
  } else {
    const uint32_t high_number = number - 8u;
    GPIO_AFRH(port) = (GPIO_AFRH(port) & ~(0xFu << (high_number * 4u))) |
                      ((uint32_t)af << (high_number * 4u));
  }
}

#endif

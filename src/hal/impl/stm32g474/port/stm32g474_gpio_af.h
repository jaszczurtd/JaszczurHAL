#ifndef JH_STM32G474_GPIO_AF_H
#define JH_STM32G474_GPIO_AF_H

#include "stm32g474_regs.h"

#include <stdint.h>

static inline void jh_stm32g474_gpio_set_af(uint8_t pin, uint8_t af) {
  const uint32_t port = (uint32_t)(pin >> 4u);
  const uint32_t number = (uint32_t)(pin & 0x0Fu);
  if (port > 6u) {
    return;
  }
  RCC_AHB2ENR |= (1u << port);
  (void)RCC_AHB2ENR;
  GPIO_MODER(port) = (GPIO_MODER(port) & ~(0x3u << (number * 2u))) |
                     (GPIO_MODE_AF << (number * 2u));
  GPIO_OTYPER(port) &= ~(1u << number);
  GPIO_OSPEEDR(port) |= (0x3u << (number * 2u));
  GPIO_PUPDR(port) = (GPIO_PUPDR(port) & ~(0x3u << (number * 2u))) |
                     (GPIO_PUPD_NONE << (number * 2u));
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

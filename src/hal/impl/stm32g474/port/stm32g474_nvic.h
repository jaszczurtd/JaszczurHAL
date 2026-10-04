#pragma once

/**
 * @file stm32g474_nvic.h
 * @brief NVIC line control shared by the STM32G474 drivers.
 *
 * Each IRQ owns one bit in the 32-bit ISER/ICER/ICPR banks and one priority
 * byte. Enabling clears a stale pending bit first, so a request left from
 * before the driver configured its peripheral does not run the handler.
 * The banks are write-one registers: writes go through JH_REG32_WR, so host
 * tests can model them.
 */

#include "stm32g474_regs.h"

#include <stdint.h>

static inline uint32_t jh_stm32g474_nvic_bit(uint32_t irqn) {
  return 1u << (irqn % 32u);
}

/** @brief Drop a pending request and enable an IRQ whose priority is set
 *         elsewhere. */
static inline void jh_stm32g474_nvic_unmask(uint32_t irqn) {
  JH_REG32_WR(NVIC_ICPR_ADDR(irqn / 32u), jh_stm32g474_nvic_bit(irqn));
  JH_REG32_WR(NVIC_ISER_ADDR(irqn / 32u), jh_stm32g474_nvic_bit(irqn));
}

/** @brief Enable the IRQ again after a short mask (see disable); a request
 *         raised meanwhile stays pending and runs the handler now. */
static inline void jh_stm32g474_nvic_resume(uint32_t irqn) {
  JH_REG32_WR(NVIC_ISER_ADDR(irqn / 32u), jh_stm32g474_nvic_bit(irqn));
}

/** @brief Set the priority, drop a pending request and enable the IRQ. */
static inline void jh_stm32g474_nvic_enable(uint32_t irqn, uint8_t priority) {
  NVIC_IPR8(irqn) = priority;
  jh_stm32g474_nvic_unmask(irqn);
}

/** @brief Disable the IRQ; a request already pending stays pending. */
static inline void jh_stm32g474_nvic_disable(uint32_t irqn) {
  JH_REG32_WR(NVIC_ICER_ADDR(irqn / 32u), jh_stm32g474_nvic_bit(irqn));
}

/** @brief Drop a pending request of a disabled IRQ. */
static inline void jh_stm32g474_nvic_clear_pending(uint32_t irqn) {
  JH_REG32_WR(NVIC_ICPR_ADDR(irqn / 32u), jh_stm32g474_nvic_bit(irqn));
}

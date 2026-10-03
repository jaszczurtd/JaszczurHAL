#pragma once

/**
 * @file stm32g474_clock_tree.h
 * @brief Bring-up of the STM32G474 clock tree described in stm32g474_clock.h.
 *
 * Header-only so startup code and host tests share one implementation. Every
 * register goes through JH_REG32_RD/JH_REG32_WR, which lets a host test model
 * the ready flags and the order the reference manual requires (RM0440 §7).
 */

#include <stdbool.h>
#include <stdint.h>

#include "stm32g474_clock.h"
#include "stm32g474_regs.h"

/** @brief Oscillator the PLL runs from. */
typedef enum {
  JH_STM32G474_PLL_FROM_HSI16 = 0,
  JH_STM32G474_PLL_FROM_HSE = 1
} jh_stm32g474_pll_source_t;

/** @brief Longest wait for the HSE oscillator: 100 ms counted in HSI16
 *  cycles, since the core still runs from HSI16 at that point. */
#define JH_STM32G474_HSE_STARTUP_CYCLES (JH_G474_HSI_CLOCK_HZ / 10u)

/** @brief FDCAN kernel clock selection that matches JH_G474_FDCAN_CLOCK_HZ. */
#if JH_G474_FDCAN_FROM_PLLQ
#define JH_STM32G474_CCIPR_FDCANSEL RCC_CCIPR_FDCANSEL_PLLQ
#else
#define JH_STM32G474_CCIPR_FDCANSEL RCC_CCIPR_FDCANSEL_PCLK1
#endif

#if defined(JH_STM32G474_HOST_REGS)
#define JH_STM32G474_CLOCK_BARRIER()
#else
#define JH_STM32G474_CLOCK_BARRIER() __asm volatile("dsb\n\tisb" ::: "memory")
#endif

static inline void jh_stm32g474_clock_modify(uintptr_t address, uint32_t clear,
                                             uint32_t set) {
  JH_REG32_WR(address, (JH_REG32_RD(address) & ~clear) | set);
}

static inline void jh_stm32g474_clock_wait(uintptr_t address, uint32_t mask,
                                           uint32_t value) {
  while ((JH_REG32_RD(address) & mask) != value) {
  }
}

/**
 * @brief Start the HSE oscillator.
 * @return true once HSERDY is set; false, with HSE switched off again, when
 *         it is not ready within JH_STM32G474_HSE_STARTUP_CYCLES.
 */
static inline bool jh_stm32g474_clock_start_hse(void) {
  jh_stm32g474_clock_modify(COREDEBUG_DEMCR_ADDR, 0u, COREDEBUG_DEMCR_TRCENA);
  jh_stm32g474_clock_modify(DWT_CTRL_ADDR, 0u, DWT_CTRL_CYCCNTENA);
  jh_stm32g474_clock_modify(RCC_CR_ADDR, 0u, RCC_CR_HSEON);
  const uint32_t started = JH_REG32_RD(DWT_CYCCNT_ADDR);
  while ((JH_REG32_RD(RCC_CR_ADDR) & RCC_CR_HSERDY) == 0u) {
    if (JH_REG32_RD(DWT_CYCCNT_ADDR) - started >=
        JH_STM32G474_HSE_STARTUP_CYCLES) {
      jh_stm32g474_clock_modify(RCC_CR_ADDR, RCC_CR_HSEON, 0u);
      return false;
    }
  }
  return true;
}

/** @brief PLLCFGR for the selected tree and oscillator. */
static inline uint32_t
jh_stm32g474_clock_pllcfgr(jh_stm32g474_pll_source_t source) {
#if defined(HAL_STM32G474_CLOCK_HSE_160MHZ)
  /* 4 MHz PLL input from either oscillator, x80 = 320 MHz VCO; R /2 gives
   * the 160 MHz SYSCLK and Q /4 the 80 MHz FDCAN kernel clock. */
  const uint32_t input =
      source == JH_STM32G474_PLL_FROM_HSE
          ? (RCC_PLLCFGR_PLLSRC_HSE |
             RCC_PLLCFGR_PLLM(JH_G474_HSE_CLOCK_HZ / 4000000u))
          : (RCC_PLLCFGR_PLLSRC_HSI |
             RCC_PLLCFGR_PLLM(JH_G474_HSI_CLOCK_HZ / 4000000u));
  return input | RCC_PLLCFGR_PLLN(80u) | RCC_PLLCFGR_PLLREN |
         RCC_PLLCFGR_PLLR_DIV2 | RCC_PLLCFGR_PLLQEN | RCC_PLLCFGR_PLLQ_DIV4;
#else
  (void)source;
  /* HSI16 / 4 = 4 MHz, x85 = 340 MHz VCO, R /2 = 170 MHz. */
  return RCC_PLLCFGR_PLLSRC_HSI | RCC_PLLCFGR_PLLM(4u) | RCC_PLLCFGR_PLLN(85u) |
         RCC_PLLCFGR_PLLREN | RCC_PLLCFGR_PLLR_DIV2;
#endif
}

/**
 * @brief Bring up voltage scaling, flash wait states, the PLL and the bus
 *        prescalers, then select the peripheral kernel clocks.
 *
 * Runs on HSI16, at reset and after STOP. The HSE tree waits for the crystal
 * at most JH_STM32G474_HSE_STARTUP_CYCLES and otherwise builds the same
 * frequencies from HSI16, so every JH_G474_*_HZ value stays true; only the
 * accuracy is that of HSI16.
 * @return The oscillator the PLL runs from.
 */
static inline jh_stm32g474_pll_source_t jh_stm32g474_clock_tree_init(void) {
  jh_stm32g474_clock_modify(RCC_APB1ENR1_ADDR, 0u, RCC_APB1ENR1_PWREN);
  (void)JH_REG32_RD(RCC_APB1ENR1_ADDR);

  /* Both trees run above 150 MHz: Range 1 boost and four wait states. */
  jh_stm32g474_clock_modify(PWR_CR5_ADDR, PWR_CR5_R1MODE, 0u);
  jh_stm32g474_clock_modify(FLASH_ACR_ADDR, FLASH_ACR_LATENCY_MASK,
                            FLASH_ACR_LATENCY_4WS | FLASH_ACR_PRFTEN |
                                FLASH_ACR_ICEN | FLASH_ACR_DCEN);
  jh_stm32g474_clock_wait(FLASH_ACR_ADDR, FLASH_ACR_LATENCY_MASK,
                          FLASH_ACR_LATENCY_4WS);

  jh_stm32g474_clock_modify(RCC_CR_ADDR, 0u, RCC_CR_HSION);
  jh_stm32g474_clock_wait(RCC_CR_ADDR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);

  jh_stm32g474_pll_source_t source = JH_STM32G474_PLL_FROM_HSI16;
#if defined(HAL_STM32G474_CLOCK_HSE_160MHZ)
  if (jh_stm32g474_clock_start_hse()) {
    source = JH_STM32G474_PLL_FROM_HSE;
  }
#endif

  /* The PLL is configured only while it is off. */
  jh_stm32g474_clock_modify(RCC_CR_ADDR, RCC_CR_PLLON, 0u);
  jh_stm32g474_clock_wait(RCC_CR_ADDR, RCC_CR_PLLRDY, 0u);
  JH_REG32_WR(RCC_PLLCFGR_ADDR, jh_stm32g474_clock_pllcfgr(source));
  jh_stm32g474_clock_modify(RCC_CR_ADDR, 0u, RCC_CR_PLLON);
  jh_stm32g474_clock_wait(RCC_CR_ADDR, RCC_CR_PLLRDY, RCC_CR_PLLRDY);

  /* Switch with HCLK halved, then run AHB at full speed after 1 us
   * (RM0440 §7.2.7 for SYSCLK above 80 MHz). Both APB buses stay /1. */
  jh_stm32g474_clock_modify(
      RCC_CFGR_ADDR,
      RCC_CFGR_HPRE_MASK | RCC_CFGR_PPRE1_MASK | RCC_CFGR_PPRE2_MASK,
      RCC_CFGR_HPRE_DIV2 | RCC_CFGR_PPRE1_DIV1 | RCC_CFGR_PPRE2_DIV1);
  jh_stm32g474_clock_modify(RCC_CFGR_ADDR, RCC_CFGR_SW_MASK, RCC_CFGR_SW_PLL);
  jh_stm32g474_clock_wait(RCC_CFGR_ADDR, RCC_CFGR_SWS_MASK, RCC_CFGR_SWS_PLL);
  for (volatile uint32_t delay = 0u; delay < 200u; ++delay) {
  }
  jh_stm32g474_clock_modify(RCC_CFGR_ADDR, RCC_CFGR_HPRE_MASK,
                            RCC_CFGR_HPRE_DIV1);

  /* I2C stays on HSI16 so its TIMINGR presets do not depend on the tree. */
  jh_stm32g474_clock_modify(RCC_CCIPR_ADDR,
                            RCC_CCIPR_I2C1SEL_MASK | RCC_CCIPR_I2C2SEL_MASK |
                                RCC_CCIPR_FDCANSEL_MASK,
                            RCC_CCIPR_I2C1SEL_HSI16 | RCC_CCIPR_I2C2SEL_HSI16 |
                                JH_STM32G474_CCIPR_FDCANSEL);

  JH_STM32G474_CLOCK_BARRIER();
  return source;
}

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Oscillator the running PLL uses (set by startup and STOP exit). */
jh_stm32g474_pll_source_t stm32g474_clock_pll_source(void);

#ifdef __cplusplus
}
#endif

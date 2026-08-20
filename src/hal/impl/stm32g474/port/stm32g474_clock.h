#pragma once

/**
 * @file stm32g474_clock.h
 * @brief STM32G474 clock-tree contract shared by startup and peripherals.
 */

#define JH_G474_HSI_CLOCK_HZ 16000000u

/* HSI16 / 4 * 85 / 2 = 170 MHz. AHB, APB1, and APB2 all run undivided. */
#define JH_G474_CORE_CLOCK_HZ 170000000u
#define JH_G474_HCLK_HZ JH_G474_CORE_CLOCK_HZ
#define JH_G474_PCLK1_HZ JH_G474_HCLK_HZ
#define JH_G474_PCLK2_HZ JH_G474_HCLK_HZ

/* APB prescalers are 1, so timer kernels are not doubled. */
#define JH_G474_TIMCLK1_HZ JH_G474_PCLK1_HZ
#define JH_G474_TIMCLK2_HZ JH_G474_PCLK2_HZ

/* I2C remains on HSI16 so the validated TIMINGR presets stay unchanged. */
#define JH_G474_I2C_KERNEL_CLOCK_HZ JH_G474_HSI_CLOCK_HZ

/* FDCAN is explicitly sourced from PCLK1 during backend initialization. */
#define JH_G474_FDCAN_CLOCK_HZ JH_G474_PCLK1_HZ

/* ADC12 uses synchronous HCLK/4, yielding 42.5 MHz. */
#define JH_G474_ADC_CLOCK_HZ (JH_G474_HCLK_HZ / 4u)

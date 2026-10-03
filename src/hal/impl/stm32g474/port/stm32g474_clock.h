#pragma once

/**
 * @file stm32g474_clock.h
 * @brief STM32G474 clock tree shared by startup and peripherals.
 *
 * The board profile picks one of two trees:
 * - default: HSI16 / 4 * 85 / 2 = 170 MHz, FDCAN on PCLK1;
 * - HAL_STM32G474_CLOCK_HSE_160MHZ: HSE 24 MHz / 6 * 80 / 2 = 160 MHz, FDCAN
 *   on PLL Q at 80 MHz, the CAN FD kernel clock CiA 601-3 recommends. When
 *   the HSE does not start, startup builds the same frequencies from HSI16.
 * AHB, APB1 and APB2 run undivided in both. stm32g474_clock_tree.h programs
 * the tree these values describe.
 */

#define JH_G474_HSI_CLOCK_HZ 16000000u

#if defined(HAL_STM32G474_CLOCK_HSE_160MHZ)
#define JH_G474_HSE_CLOCK_HZ 24000000u
#define JH_G474_CORE_CLOCK_HZ 160000000u
/** FDCAN kernel clock comes from PLL Q, not from PCLK1. */
#define JH_G474_FDCAN_FROM_PLLQ 1
#define JH_G474_FDCAN_CLOCK_HZ 80000000u
#else
#define JH_G474_CORE_CLOCK_HZ 170000000u
#define JH_G474_FDCAN_FROM_PLLQ 0
#endif

#define JH_G474_HCLK_HZ JH_G474_CORE_CLOCK_HZ
#define JH_G474_PCLK1_HZ JH_G474_HCLK_HZ
#define JH_G474_PCLK2_HZ JH_G474_HCLK_HZ

/* APB prescalers are 1, so timer kernels are not doubled. */
#define JH_G474_TIMCLK1_HZ JH_G474_PCLK1_HZ
#define JH_G474_TIMCLK2_HZ JH_G474_PCLK2_HZ

/* I2C remains on HSI16 so the validated TIMINGR presets stay unchanged. */
#define JH_G474_I2C_KERNEL_CLOCK_HZ JH_G474_HSI_CLOCK_HZ

#if !JH_G474_FDCAN_FROM_PLLQ
#define JH_G474_FDCAN_CLOCK_HZ JH_G474_PCLK1_HZ
#endif

/* ADC12 uses synchronous HCLK/4: 42.5 MHz, or 40 MHz on the HSE tree. */
#define JH_G474_ADC_CLOCK_HZ (JH_G474_HCLK_HZ / 4u)

#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_RP2350_RISCV)                                                   \
  X(HAL_TARGET_STM32G474)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(DISPLAY_CLOCK, "DS3231 clock with an ILI9341 display",                     \
    EXAMPLE_RTC_DISPLAY_CLOCK = 1)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Include every RTC driver used by the application; select the chip in its
 * configuration. */
#define HAL_ENABLE_RTC
#define HAL_ENABLE_PCF8563
#define HAL_ENABLE_DS3231
#define HAL_ENABLE_INTERNAL_RTC
#define HAL_ENABLE_POWER_MANAGEMENT

#if defined(EXAMPLE_RTC_DISPLAY_CLOCK)
#if !defined(HAL_TARGET_STM32G474)
#error "16_rtc_backends DISPLAY_CLOCK: STM32G474 only"
#endif
#define HAL_ENABLE_ILI9341
#define HAL_DISPLAY_ILI9341
#endif

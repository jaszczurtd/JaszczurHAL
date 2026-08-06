#pragma once

/**
 * @file hal_target.h
 * @brief Exact compile-time target selection for JaszczurHAL.
 *
 * Consumers define exactly one HAL_TARGET_* macro. RP chip/ISA selection is
 * separate from its board profile.
 *
 * Exact targets:
 *
 *     HAL_TARGET_RP2040
 *     HAL_TARGET_RP2350_ARM
 *     HAL_TARGET_RP2350_RISCV
 *     HAL_TARGET_STM32G474
 *     HAL_TARGET_MOCK
 *
 * Board profiles remain separate and must not introduce HAL_TARGET_PICO_*
 * aliases.
 */

#include "hal_project_config_hook.h"

/* 1. Auto-detect the exact target when none was selected. */
#if !defined(HAL_TARGET_RP2040) && !defined(HAL_TARGET_RP2350_ARM) &&          \
    !defined(HAL_TARGET_RP2350_RISCV) && !defined(HAL_TARGET_STM32G474) &&     \
    !defined(HAL_TARGET_MOCK)

#if defined(PICO_RP2350)
#if defined(__riscv)
#define HAL_TARGET_RP2350_RISCV 1
#elif defined(__arm__) || defined(__thumb__)
#define HAL_TARGET_RP2350_ARM 1
#else
#error                                                                         \
    "JaszczurHAL: PICO_RP2350 target ISA is ambiguous; define HAL_TARGET_RP2350_ARM or HAL_TARGET_RP2350_RISCV."
#endif
#elif defined(PICO_RP2040)
#define HAL_TARGET_RP2040 1
#elif defined(STM32G474xx) || defined(STM32G4)
#define HAL_TARGET_STM32G474 1
#elif !defined(__arm__) && !defined(__thumb__) && !defined(__riscv)
#define HAL_TARGET_MOCK 1
#else
#error                                                                         \
    "JaszczurHAL: no target selected and none could be auto-detected. Define one HAL_TARGET_* macro in hal_project_config.h or via -D."
#endif

#endif

/* 2. Normalize exact target selectors to 0/1. */
#if defined(HAL_TARGET_RP2040)
#define HAL_TARGET_IS_RP2040 1
#else
#define HAL_TARGET_IS_RP2040 0
#endif

#if defined(HAL_TARGET_RP2350_ARM)
#define HAL_TARGET_IS_RP2350_ARM 1
#else
#define HAL_TARGET_IS_RP2350_ARM 0
#endif

#if defined(HAL_TARGET_RP2350_RISCV)
#define HAL_TARGET_IS_RP2350_RISCV 1
#else
#define HAL_TARGET_IS_RP2350_RISCV 0
#endif

#if defined(HAL_TARGET_STM32G474)
#define HAL_TARGET_IS_STM32G474 1
#else
#define HAL_TARGET_IS_STM32G474 0
#endif

#if defined(HAL_TARGET_MOCK)
#define HAL_TARGET_IS_MOCK 1
#else
#define HAL_TARGET_IS_MOCK 0
#endif

/* 3. Enforce exactly one exact target. */
#if (HAL_TARGET_IS_RP2040 + HAL_TARGET_IS_RP2350_ARM +                         \
     HAL_TARGET_IS_RP2350_RISCV + HAL_TARGET_IS_STM32G474 +                    \
     HAL_TARGET_IS_MOCK) != 1
#error                                                                         \
    "JaszczurHAL: exactly one HAL_TARGET_* must be selected (RP2040 / RP2350_ARM / RP2350_RISCV / STM32G474 / MOCK)."
#endif

/* 4. Derived family and ISA selectors. */
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_RP2350_ARM ||                        \
    HAL_TARGET_IS_RP2350_RISCV
#define HAL_TARGET_IS_RP 1
#else
#define HAL_TARGET_IS_RP 0
#endif

#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_RP2350_ARM
#define HAL_RP_ARCH_ARM 1
#else
#define HAL_RP_ARCH_ARM 0
#endif

#if HAL_TARGET_IS_RP2350_RISCV
#define HAL_RP_ARCH_RISCV 1
#else
#define HAL_RP_ARCH_RISCV 0
#endif

/* 5. Human-readable exact target name. */
#if HAL_TARGET_IS_RP2040
#define HAL_TARGET_NAME "rp2040"
#elif HAL_TARGET_IS_RP2350_ARM
#define HAL_TARGET_NAME "rp2350-arm"
#elif HAL_TARGET_IS_RP2350_RISCV
#define HAL_TARGET_NAME "rp2350-riscv"
#elif HAL_TARGET_IS_STM32G474
#define HAL_TARGET_NAME "stm32g474"
#else
#define HAL_TARGET_NAME "mock"
#endif

/* 6. Real STM32G474 hardware versus host-stub sanity build. */
#if HAL_TARGET_IS_STM32G474 && (defined(__arm__) || defined(__thumb__))
#ifndef JH_STM32G474_HW
#define JH_STM32G474_HW 1
#endif
#endif

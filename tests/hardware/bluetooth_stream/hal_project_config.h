#pragma once

/* Targets this fixture builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_STM32G474)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(FREERTOS, "FreeRTOS runtime", HAL_ENABLE_FREERTOS)                         \
  X(DISPLAY, "ILI9341 load on SPI1 next to the PIM730 transport",              \
    JHBL5_ENABLE_DISPLAY = 1, HAL_ENABLE_ILI9341, HAL_DISPLAY_ILI9341)         \
  X(DISPLAY_FREERTOS,                                                          \
    "ILI9341 load on SPI1 next to the PIM730 transport, FreeRTOS",             \
    JHBL5_ENABLE_DISPLAY = 1, HAL_ENABLE_ILI9341, HAL_DISPLAY_ILI9341,         \
    HAL_ENABLE_FREERTOS)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 1024u
#endif

#define HAL_ENABLE_BLE_STREAM

#if defined(JHBL5_ENABLE_DISPLAY) && !defined(HAL_TARGET_STM32G474)
#error "bluetooth_stream DISPLAY: STM32G474 with PIM730 only"
#endif

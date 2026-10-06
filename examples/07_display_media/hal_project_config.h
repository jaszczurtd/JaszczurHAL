#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_RP2350_RISCV)                                                   \
  X(HAL_TARGET_STM32G474)                                                      \
  X(HAL_TARGET_ESP32_S3)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#define HAL_ENABLE_ILI9341
#define HAL_DISPLAY_ILI9341
#define HAL_ENABLE_PNG_AS_BASE64
#define HAL_ENABLE_JPEG_AS_BASE64

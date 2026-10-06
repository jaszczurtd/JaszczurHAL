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

#define HAL_ENABLE_MCP23017
#define HAL_ENABLE_PCA9654E
#define HAL_ENABLE_PCF8574
#define HAL_ENABLE_HC595
#define HAL_ENABLE_MCP3221
#define HAL_ENABLE_MCP4725
#define HAL_ENABLE_ADP5360
#define HAL_ENABLE_RGB_LED

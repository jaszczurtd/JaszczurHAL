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

/* Enabling SSD1306 also enables the display, graphics, and I2C support it
 * needs. */
#define HAL_ENABLE_SSD1306
#define HAL_ENABLE_HD44780

#ifndef HAL_PROJECT_CONFIG_H
#define HAL_PROJECT_CONFIG_H

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_RP2350_RISCV)                                                   \
  X(HAL_TARGET_STM32G474)                                                      \
  X(HAL_TARGET_ESP32_S3)

#define HAL_DEBUG_DEFAULT_BAUD 115200u
#define HAL_ENABLE_IRSMALL_DECODER

#endif

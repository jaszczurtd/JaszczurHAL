#pragma once

/* Targets this fixture builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_STM32G474)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#define HAL_ENABLE_BLE

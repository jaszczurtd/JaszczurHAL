#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_STM32G474)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(COMMANDS, "Authenticated BLE command example",                             \
    HAL_ENABLE_BLE_COMMANDS = 1, HAL_ENABLE_CRC = 1,                           \
    HAL_BLE_STREAM_EXAMPLE_COMMANDS = 1)                                       \
  X(COMMANDS_FREERTOS, "Authenticated BLE command example with FreeRTOS",      \
    HAL_ENABLE_BLE_COMMANDS = 1, HAL_ENABLE_CRC = 1,                           \
    HAL_BLE_STREAM_EXAMPLE_COMMANDS = 1, HAL_ENABLE_FREERTOS = 1)

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 1024u
#endif

#define HAL_ENABLE_BLE_STREAM

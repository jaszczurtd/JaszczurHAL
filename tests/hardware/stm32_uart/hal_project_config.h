#pragma once

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(FREERTOS, "FreeRTOS with the second application task",                     \
    HAL_ENABLE_FREERTOS = 1, HAL_ENABLE_APP_TASK1 = 1)                         \
  X(HAT_RELAYS, "Relay drivers off on a nucleo-g474re carrying the HAT",       \
    UART_FIXTURE_HAT_RELAYS = 1)

#define HAL_ENABLE_UART
/* The Z command overflows the main stack into the guard. */
#define HAL_ENABLE_STACK_GUARD 1

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

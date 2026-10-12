#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(FREERTOS, "FreeRTOS with the second application task",                     \
    HAL_ENABLE_FREERTOS = 1, HAL_ENABLE_APP_TASK1 = 1)

#define HAL_ENABLE_UART
/* The Z command overflows the main stack into the guard. */
#define HAL_ENABLE_STACK_GUARD 1

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

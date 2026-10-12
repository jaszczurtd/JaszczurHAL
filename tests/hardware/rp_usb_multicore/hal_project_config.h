#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(FREERTOS, "FreeRTOS SMP runtime", HAL_ENABLE_FREERTOS)

#ifndef HAL_ENABLE_APP_TASK1
#define HAL_ENABLE_APP_TASK1
#endif

#if defined(HAL_ENABLE_FREERTOS)
#ifndef HAL_FREERTOS_TASK0_STACK
#define HAL_FREERTOS_TASK0_STACK 768u
#endif
#ifndef HAL_FREERTOS_TASK1_STACK
#define HAL_FREERTOS_TASK1_STACK 768u
#endif
#endif

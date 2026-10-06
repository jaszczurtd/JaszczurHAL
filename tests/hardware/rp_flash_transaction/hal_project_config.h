#pragma once

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(FREERTOS, "FreeRTOS SMP runtime", HAL_ENABLE_FREERTOS = 1)

#ifndef HAL_ENABLE_APP_TASK1
#define HAL_ENABLE_APP_TASK1
#endif
#ifndef HAL_ENABLE_KV
#define HAL_ENABLE_KV
#endif
#ifndef HAL_ENABLE_ADC_SCAN
#define HAL_ENABLE_ADC_SCAN
#endif

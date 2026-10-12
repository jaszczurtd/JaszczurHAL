#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(SCAN, "Continuous ADC scan example", HAL_ENABLE_ADC_SCAN = 1)

#define HAL_ENABLE_EXTERNAL_ADC
#define HAL_ENABLE_I2C

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

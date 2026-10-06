#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_STM32G474)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* The build enables HAL_PROVIDE_APP_ENTRY so HAL supplies the STM32 entry
 * point. */

#define HAL_ENABLE_STM32G474_FDCAN

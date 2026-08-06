#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system:
 * STM32 defines HAL_PROVIDE_APP_ENTRY. */

#define HAL_ENABLE_STM32G474_FDCAN

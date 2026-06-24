#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */

#ifndef HAL_ENABLE_SDLOGGER
#define HAL_ENABLE_SDLOGGER
#endif

#ifndef HAL_SDLOGGER_WRITE_INTERVAL_MS
#define HAL_SDLOGGER_WRITE_INTERVAL_MS 2000u
#endif

#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* The build system selects the target-specific application entry point. */

#ifndef HAL_ENABLE_SDLOGGER
#define HAL_ENABLE_SDLOGGER
#endif

#ifndef HAL_SDLOGGER_WRITE_INTERVAL_MS
#define HAL_SDLOGGER_WRITE_INTERVAL_MS 2000u
#endif

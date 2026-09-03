#ifndef T_TOOLS_HAL
#define T_TOOLS_HAL

/**
 * @file tools.h
 * @brief C++ utility surface for JaszczurHAL.
 *
 * This header exposes:
 * - HAL utility domains aggregated by @ref tools_api.h,
 * - shared macro/constants from @ref tools_common_defs.h,
 * - and SmartTimers.
 */

#include "libConfig.h"
#include <hal/hal.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef HAL_ENABLE_UNITY
#include "unity.h"
#endif
#include "hal/timers/smart_timers/SmartTimers.h"

#include "tools_api.h"
#include "tools_common_defs.h"

#endif

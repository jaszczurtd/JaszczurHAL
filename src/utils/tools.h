#ifndef T_TOOLS_HAL
#define T_TOOLS_HAL

/**
 * @file tools.h
 * @brief C++ utility surface for JaszczurHAL.
 *
 * This header exposes:
 * - shared C/C++ API declarations from @ref tools_api.h,
 * - shared macro/constants from @ref tools_common_defs.h,
 * - and legacy utility declarations implemented on top of HAL.
 */

#include "libConfig.h"
#include <hal/hal.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef HAL_ENABLE_UNITY
#include "unity.h"
#endif
#include "SmartTimers.h"

#include "tools_common_defs.h"
#include "tools_api.h"

#endif

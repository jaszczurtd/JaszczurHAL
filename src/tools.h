#pragma once

/**
 * @file tools.h
 * @brief Public utility aggregator include.
 *
 * This header re-exports utility modules from `src/utils/', 
 * providing a single include for:
 * - SmartTimers
 * - pidController
 * - tools
 * - draw7Segment
 * - multicoreWatchdog
 *
 * Optional:
 * - define `HAL_ENABLE_CJSON` to expose bundled `cJSON` and `cJSON_Utils`
 *   headers through this aggregator.
 */

#include "utils/SmartTimers.h"
#include "utils/pidController.h"
#include "tools_c.h"
#include "utils/draw7Segment.h"
#include "utils/multicoreWatchdog.h"

#ifdef HAL_ENABLE_CJSON
#include "utils/cJSON.h"
#include "utils/cJSON_Utils.h"
#endif

#pragma once

/**
 * @file tools.h
 * @brief Public utility aggregator include.
 *
 * This header re-exports utility modules from `src/utils/` and shared
 * reusable modules from their thematic directories under `src/hal/`,
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
 * - define `HAL_ENABLE_PNG` to expose bundled `LodePNG` headers through this
 *   aggregator.
 * - define `HAL_ENABLE_PNG_AS_BASE64` to expose Base64 PNG helper functions;
 *   this flag propagates `HAL_ENABLE_CRYPTO` and `HAL_ENABLE_PNG`.
 * - define `HAL_ENABLE_JPEG` to expose the managed TJpgDec core header through
 *   this aggregator.
 * - define `HAL_ENABLE_JPEG_AS_BASE64` to expose Base64 JPEG helper functions;
 *   this flag propagates `HAL_ENABLE_CRYPTO` and `HAL_ENABLE_JPEG`.
 */

#include "hal/timers/smart_timers/SmartTimers.h"
#include "tools_c.h"
#include "utils/draw7Segment.h"
#include "utils/multicoreWatchdog.h"
#include "utils/pidController.h"

#ifdef HAL_ENABLE_CJSON
#include "hal/codecs/cjson/cJSON.h"
#include "hal/codecs/cjson/cJSON_Utils.h"
#endif

#ifdef HAL_ENABLE_PNG
#include "hal/codecs/lodepng/lodepng.h"
#endif

#ifdef HAL_ENABLE_JPEG
#include "hal/codecs/jpeg/tjpgd.h"
#endif

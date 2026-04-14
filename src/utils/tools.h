#ifndef T_TOOLS_HAL
#define T_TOOLS_HAL

/**
 * @file tools.h
 * @brief C++ utility surface for JaszczurHAL.
 *
 * This header exposes:
 * - shared C/C++ API declarations from @ref tools_api.h,
 * - shared macro/constants from @ref tools_common_defs.h,
 * - and C++-only declarations that require Arduino types.
 */

#include <Arduino.h>
#include "libConfig.h"
#include <hal/hal.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <SPI.h>
#ifdef SD_LOGGER
#include <SD.h>
#endif
#ifndef HAL_DISABLE_UNITY
#include "unity.h"
#endif
#include "SmartTimers.h"
#ifdef PICO_W
#include <WiFi.h>
#endif

#include "tools_common_defs.h"
#include "tools_api.h"

#ifdef __cplusplus
/**
 * @brief Append data to the SD crash report.
 * @param data String to append.
 */
void updateCrashReport(String data);
#endif

#endif

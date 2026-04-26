#pragma once

/**
 * @file tools_c.h
 * @brief Transitional C-style compatibility umbrella for ECU modules.
 *
 * This header lives in JaszczurHAL to keep compatibility concerns in HAL,
 * not in project-local ECU code. It exposes the HAL headers and legacy tools
 * declarations used by ECU modules currently migrated to `.c` sources.
 */

#ifdef __cplusplus
#include "utils/tools.h"
#include "utils/multicoreWatchdog.h"
#else

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <libConfig.h>

/**
 * @brief Forward declaration for HAL mutex implementation (C mode).
 *
 * Needed because @ref hal_sync.h exposes opaque mutex handles.
 */
typedef struct hal_mutex_impl_t hal_mutex_impl_t;

#include <hal/hal_config.h>
#include <hal/hal_gpio.h>
#include <hal/hal_pwm.h>
#ifndef HAL_DISABLE_PWM_FREQ
#include <hal/hal_pwm_freq.h>
#endif
#include <hal/hal_adc.h>
#include <hal/hal_timer.h>
#include <hal/hal_soft_timer.h>
#include <hal/hal_pid_controller.h>
#include <hal/hal_system.h>
#include <hal/hal_crypto.h>
#include <hal/hal_sync.h>
#include <hal/hal_spi.h>
#ifndef HAL_DISABLE_I2C
#include <hal/hal_i2c.h>
#endif
#ifndef HAL_DISABLE_EXTERNAL_ADC
#include <hal/hal_external_adc.h>
#endif
#ifndef HAL_DISABLE_CAN
#include <hal/hal_can.h>
#endif
#ifndef HAL_DISABLE_GPS
#include <hal/hal_gps.h>
#endif
#ifndef HAL_DISABLE_EEPROM
#include <hal/hal_eeprom.h>
#endif
#ifndef HAL_DISABLE_KV
#include <hal/hal_kv.h>
#endif

/**
 * @brief Program-memory storage qualifier fallback for plain C builds.
 *
 * On non-Arduino C translation units, PROGMEM may be undefined; in that case it
 * expands to an empty qualifier to keep declarations portable.
 */
#ifndef PROGMEM
#define PROGMEM
#endif
/** @brief Arduino `F()` macro fallback for plain C builds. */
#ifndef F
#define F(s) (s)
#endif

#include "utils/tools_common_defs.h"
#include "utils/tools_api.h"
#include "utils/multicoreWatchdog.h"

#endif /* __cplusplus */

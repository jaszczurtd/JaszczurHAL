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
#include "utils/multicoreWatchdog.h"
#include "utils/tools.h"
#else

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#ifdef HAL_ENABLE_PWM_FREQ
#include <hal/hal_pwm_freq.h>
#endif
#include <hal/hal_adc.h>
#include <hal/hal_pid_controller.h>
#include <hal/hal_soft_timer.h>
#include <hal/hal_system.h>
#include <hal/hal_timer.h>
#ifdef HAL_ENABLE_CRYPTO
#include <hal/hal_crypto.h>
#endif
#ifdef HAL_ENABLE_MQTT
#include <hal/hal_mqtt.h>
#endif
#ifdef HAL_ENABLE_LITTLEFS
#include <hal/hal_littlefs.h>
#endif
#ifdef HAL_ENABLE_SDLOGGER
#include <hal/hal_sdlogger.h>
#endif
#ifdef HAL_ENABLE_OTA
#include <hal/hal_ota.h>
#endif
#ifdef HAL_ENABLE_UDP
#include <hal/hal_udp.h>
#endif
#ifdef HAL_ENABLE_TCP
#include <hal/hal_tcp.h>
#endif
#ifdef HAL_ENABLE_WIREGUARD
#include <hal/hal_wireguard.h>
#endif
#ifdef HAL_ENABLE_DHT
#include <hal/hal_dht.h>
#endif
#include <hal/hal_spi.h>
#include <hal/hal_sync.h>
#ifdef HAL_ENABLE_I2C
#include <hal/hal_i2c.h>
#endif
#ifdef HAL_ENABLE_EXTERNAL_ADC
#include <hal/hal_external_adc.h>
#endif
#ifdef HAL_ENABLE_CAN
#include <hal/hal_can.h>
#endif
#ifdef HAL_ENABLE_GPS
#include <hal/hal_gps.h>
#endif
#ifdef HAL_ENABLE_EEPROM
#include <hal/hal_eeprom.h>
#endif
#ifdef HAL_ENABLE_KV
#include <hal/hal_kv.h>
#endif

#include "utils/multicoreWatchdog.h"
#include "utils/tools_api.h"
#include "utils/tools_common_defs.h"

#endif /* __cplusplus */

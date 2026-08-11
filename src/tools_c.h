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

#include <hal/core/hal_config.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/gpio/hal_pwm.h>
#ifdef HAL_ENABLE_PWM_FREQ
#include <hal/gpio/hal_pwm_freq.h>
#endif
#include <hal/analog/hal_adc.h>
#include <hal/control/hal_pid_controller.h>
#include <hal/core/hal_status.h>
#include <hal/system/hal_system.h>
#include <hal/timers/hal_soft_timer.h>
#include <hal/timers/hal_timer.h>
#ifdef HAL_ENABLE_CRYPTO
#include <hal/security/hal_crypto.h>
#endif
#ifdef HAL_ENABLE_MQTT
#include <hal/network/mqtt/hal_mqtt.h>
#endif
#ifdef HAL_ENABLE_LITTLEFS
#include <hal/storage/hal_littlefs.h>
#endif
#ifdef HAL_ENABLE_SDLOGGER
#include <hal/storage/hal_sdlogger.h>
#endif
#ifdef HAL_ENABLE_OTA
#include <hal/network/ota/hal_ota.h>
#endif
#ifdef HAL_ENABLE_UDP
#include <hal/network/hal_udp.h>
#endif
#ifdef HAL_ENABLE_TCP
#include <hal/network/hal_tcp.h>
#endif
#ifdef HAL_ENABLE_WIREGUARD
#include <hal/network/wireguard/hal_wireguard.h>
#endif
#ifdef HAL_ENABLE_DHT
#include <hal/temperature/hal_dht.h>
#endif
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_sync.h>
#ifdef HAL_ENABLE_I2C
#include <hal/i2c/hal_i2c.h>
#endif
#ifdef HAL_ENABLE_EXTERNAL_ADC
#include <hal/analog/hal_external_adc.h>
#endif
#ifdef HAL_ENABLE_CAN
#include <hal/can/hal_can.h>
#endif
#ifdef HAL_ENABLE_GPS
#include <hal/gps/hal_gps.h>
#endif
#ifdef HAL_ENABLE_EEPROM
#include <hal/storage/hal_eeprom.h>
#endif
#ifdef HAL_ENABLE_KV
#include <hal/storage/hal_kv.h>
#endif

#include "utils/multicoreWatchdog.h"
#include "utils/tools_api.h"
#include "utils/tools_common_defs.h"

#endif /* __cplusplus */

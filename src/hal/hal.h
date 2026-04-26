/**
 * @file hal.h
 * @brief Internal HAL-only umbrella include.
 *
 * This header aggregates all HAL modules and applies `HAL_DISABLE_*` guards
 * from `hal_config.h` to include only enabled interfaces.
 *
 * Prefer `#include <JaszczurHAL.h>` in application code.
 * Use this header directly only when you intentionally want HAL-only includes
 * without utility modules.
 */

#pragma once

#include "hal_config.h"
#include "hal_gpio.h"
#include "hal_math.h"
#include "hal_bits.h"
#ifdef HAL_ENABLE_CRYPTO
#include "hal_crypto.h"
#endif
#include "hal_pwm.h"
#ifndef HAL_DISABLE_PWM_FREQ
#include "hal_pwm_freq.h"
#endif
#include "hal_adc.h"
#include "hal_timer.h"
#include "hal_soft_timer.h"
#include "hal_pid_controller.h"
#include "hal_system.h"
#include "hal_sync.h"
#include "hal_serial.h"
#include "hal_serial_session.h"
#ifndef HAL_DISABLE_UART
#include "hal_uart.h"
#endif
#ifndef HAL_DISABLE_SWSERIAL
#include "hal_swserial.h"
#endif
#include "hal_spi.h"
#ifndef HAL_DISABLE_I2C
#include "hal_i2c.h"
#endif
#ifndef HAL_DISABLE_I2C_SLAVE
#include "hal_i2c_slave.h"
#endif
#ifndef HAL_DISABLE_EXTERNAL_ADC
#include "hal_external_adc.h"
#endif
#ifndef HAL_DISABLE_RGB_LED
#include "hal_rgb_led.h"
#endif
#ifndef HAL_DISABLE_CAN
#include "hal_can.h"
#endif
#ifndef HAL_DISABLE_DISPLAY
#include "hal_display.h"
#endif
#ifndef HAL_DISABLE_WIFI
#include "hal_wifi.h"
#endif
#ifndef HAL_DISABLE_TIME
#include "hal_time.h"
#endif
#ifndef HAL_DISABLE_THERMOCOUPLE
#include "hal_thermocouple.h"
#endif
#ifndef HAL_DISABLE_GPS
#include "hal_gps.h"
#endif
#ifndef HAL_DISABLE_EEPROM
#include "hal_eeprom.h"
#endif
#ifndef HAL_DISABLE_KV
#include "hal_kv.h"
#endif

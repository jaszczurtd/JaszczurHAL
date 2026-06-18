/**
 * @file hal.h
 * @brief Internal HAL-only umbrella include.
 *
 * This header aggregates all HAL modules and applies `HAL_ENABLE_*` guards
 * from `hal_config.h` to include only enabled interfaces.
 *
 * Prefer `#include <JaszczurHAL.h>` in application code.
 * Use this header directly only when you intentionally want HAL-only includes
 * without utility modules.
 */

#pragma once

#include "hal_bits.h"
#include "hal_config.h"
#include "hal_gpio.h"
#include "hal_math.h"
#ifdef HAL_ENABLE_CRYPTO
#include "hal_crypto.h"
#endif
#include "hal_pwm.h"
#ifdef HAL_ENABLE_PWM_FREQ
#include "hal_pwm_freq.h"
#endif
#include "hal_adc.h"
#ifdef HAL_ENABLE_DAC
#include "hal_dac.h"
#endif
#ifdef HAL_ENABLE_PCNT
#include "hal_pcnt.h"
#endif
#include "hal_pid_controller.h"
#include "hal_serial.h"
#include "hal_serial_session.h"
#include "hal_soft_timer.h"
#include "hal_sync.h"
#include "hal_system.h"
#include "hal_timer.h"
#ifdef HAL_ENABLE_UART
#include "hal_uart.h"
#endif
#ifdef HAL_ENABLE_SWSERIAL
#include "hal_swserial.h"
#endif
#include "hal_spi.h"
#ifdef HAL_ENABLE_ONEWIRE
#include "hal_onewire.h"
#endif
#ifdef HAL_ENABLE_I2C
#include "hal_i2c.h"
#endif
#ifdef HAL_ENABLE_I2C_SLAVE
#include "hal_i2c_slave.h"
#endif
#ifdef HAL_ENABLE_EXTERNAL_ADC
#include "hal_external_adc.h"
#endif
#ifdef HAL_ENABLE_RGB_LED
#include "hal_rgb_led.h"
#endif
#ifdef HAL_ENABLE_CAN
#include "hal_can.h"
#endif
#ifdef HAL_ENABLE_PGA2311
#include "hal_pga2311.h"
#endif
#ifdef HAL_ENABLE_DISPLAY
#include "hal_display.h"
#endif
#ifdef HAL_ENABLE_HD44780
#include "hal_hd44780.h"
#endif
#ifdef HAL_ENABLE_WIFI
#include "hal_wifi.h"
#endif
#ifdef HAL_ENABLE_LITTLEFS
#include "hal_littlefs.h"
#endif
#ifdef HAL_ENABLE_SDLOGGER
#include "hal_sdlogger.h"
#endif
#ifdef HAL_ENABLE_UDP
#include "hal_udp.h"
#endif
#ifdef HAL_ENABLE_WIREGUARD
#include "hal_wireguard.h"
#endif
#ifdef HAL_ENABLE_MQTT
#include "hal_mqtt.h"
#endif
#ifdef HAL_ENABLE_OTA
#include "hal_ota.h"
#endif
#ifdef HAL_ENABLE_CELLULAR_MODEM
#include "hal_modem_at.h"
#endif
#ifdef HAL_ENABLE_A7670
#include "hal_simcom_a76xx.h"
#endif
#ifdef HAL_ENABLE_TIME
#include "hal_time.h"
#endif
#ifdef HAL_ENABLE_RTC
#include "hal_rtc.h"
#endif
#ifdef HAL_ENABLE_THERMOCOUPLE
#include "hal_thermocouple.h"
#endif
#ifdef HAL_ENABLE_DS18B20
#include "hal_ds18b20.h"
#endif
#ifdef HAL_ENABLE_BH1750
#include "hal_bh1750.h"
#endif
#ifdef HAL_ENABLE_TSC2007
#include "hal_tsc2007.h"
#endif
#ifdef HAL_ENABLE_STMPE610
#include "hal_stmpe610.h"
#endif
#ifdef HAL_ENABLE_IRSMALL_DECODER
#include "hal_irsmall_decoder.h"
#endif
#ifdef HAL_ENABLE_GPS
#include "hal_gps.h"
#endif
#ifdef HAL_ENABLE_EEPROM
#include "hal_eeprom.h"
#endif
#ifdef HAL_ENABLE_KV
#include "hal_kv.h"
#endif

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

#include "hal/core/hal_bits.h"
#include "hal/core/hal_config.h"
#include "hal/system/hal_board.h"
#ifdef HAL_ENABLE_BLE
#include "hal/bluetooth/hal_ble.h"
#endif
#ifdef HAL_ENABLE_BLE_STREAM
#include "hal/bluetooth/hal_ble_stream.h"
#endif
#ifdef HAL_ENABLE_LORA
#include "hal/radio/hal_lora_radio.h"
#endif
#ifdef HAL_ENABLE_LORA_LINK
#include "hal/radio/hal_lora_link.h"
#endif
#include "hal/core/hal_math.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/network/hal_net.h"
#ifdef HAL_ENABLE_CRYPTO
#include "hal/security/hal_crypto.h"
#endif
#include "hal/gpio/hal_pwm.h"
#ifdef HAL_ENABLE_PWM_FREQ
#include "hal/gpio/hal_pwm_freq.h"
#endif
#include "hal/analog/hal_adc.h"
#ifdef HAL_ENABLE_DAC
#include "hal/analog/hal_dac.h"
#endif
#ifdef HAL_ENABLE_DACLESS
#include "hal/audio/hal_dacless.h"
#endif
#ifdef HAL_ENABLE_DMA_PWM_AUDIO
#include "hal/audio/hal_dma_pwm_audio.h"
#endif
#ifdef HAL_ENABLE_PCNT
#include "hal/analog/hal_pcnt.h"
#endif
#include "hal/control/hal_pid_controller.h"
#include "hal/core/hal_status.h"
#include "hal/serial/hal_serial.h"
#include "hal/serial/hal_serial_session.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal/time/hal_time.h"
#include "hal/timers/hal_soft_timer.h"
#include "hal/timers/hal_timer.h"
#include "hal/usb/hal_usb.h"
#ifdef HAL_ENABLE_UART
#include "hal/serial/hal_uart.h"
#endif
#ifdef HAL_ENABLE_SWSERIAL
#include "hal/serial/hal_swserial.h"
#endif
#include "hal/spi/hal_spi.h"
#include "hal/spi/hal_spi_device.h"
#ifdef HAL_ENABLE_ONEWIRE
#include "hal/onewire/hal_onewire.h"
#endif
#ifdef HAL_ENABLE_I2C
#include "hal/i2c/hal_i2c.h"
#endif
#ifdef HAL_ENABLE_I2C_SLAVE
#include "hal/i2c/hal_i2c_slave.h"
#endif
#ifdef HAL_ENABLE_EXTERNAL_ADC
#include "hal/analog/hal_external_adc.h"
#endif
#ifdef HAL_ENABLE_MCP3221
#include "hal/analog/hal_mcp3221.h"
#endif
#ifdef HAL_ENABLE_RGB_LED
#include "hal/gpio/hal_rgb_led.h"
#endif
#ifdef HAL_ENABLE_MCP23017
#include "hal/gpio/hal_mcp23017.h"
#endif
#ifdef HAL_ENABLE_PCA9654E
#include "hal/gpio/hal_pca9654e.h"
#endif
#ifdef HAL_ENABLE_PCF8574
#include "hal/gpio/hal_pcf8574.h"
#endif
#ifdef HAL_ENABLE_HC595
#include "hal/gpio/hal_hc595.h"
#endif
#ifdef HAL_ENABLE_CAN
#include "hal/can/hal_can.h"
#endif
#ifdef HAL_ENABLE_PGA2311
#include "hal/audio/hal_pga2311.h"
#endif
#ifdef HAL_ENABLE_MCP4725
#include "hal/analog/hal_mcp4725.h"
#endif
#ifdef HAL_ENABLE_MFRC522
#include "hal/nfc/hal_mfrc522.h"
#endif
#ifdef HAL_ENABLE_PN532
#include "hal/nfc/hal_pn532.h"
#endif
#ifdef HAL_ENABLE_DISPLAY
#include "hal/display/hal_display.h"
#endif
#ifdef HAL_ENABLE_HD44780
#include "hal/display/hal_hd44780.h"
#endif
#ifdef HAL_ENABLE_WIFI
#include "hal/network/hal_wifi.h"
#endif
#ifdef HAL_ENABLE_LITTLEFS
#include "hal/storage/hal_littlefs.h"
#endif
#ifdef HAL_ENABLE_SDLOGGER
#include "hal/storage/hal_sdlogger.h"
#endif
#ifdef HAL_ENABLE_UDP
#include "hal/network/hal_udp.h"
#endif
#ifdef HAL_ENABLE_TCP
#include "hal/network/hal_tcp.h"
#endif
#ifdef HAL_ENABLE_TLS
#include "hal/network/tls/hal_tls.h"
#endif
#ifdef HAL_ENABLE_HTTP_CLIENT
#include "hal/network/http/hal_http_client.h"
#endif
#ifdef HAL_ENABLE_HTTP_SERVER
#include "hal/network/http/hal_http_server.h"
#endif
#ifdef HAL_ENABLE_HTTP_FILES
#include "hal/network/http/hal_http_files.h"
#endif
#ifdef HAL_ENABLE_WEBSOCKET
#include "hal/network/websocket/hal_websocket.h"
#endif
#ifdef HAL_ENABLE_NET_CONSOLE
#include "hal/network/net_console/hal_net_console.h"
#endif
#ifdef HAL_ENABLE_NET_COMMANDS
#include "hal/network/net_commands/hal_net_commands.h"
#endif
#ifdef HAL_ENABLE_WIREGUARD
#include "hal/network/wireguard/hal_wireguard.h"
#endif
#ifdef HAL_ENABLE_MQTT
#include "hal/network/mqtt/hal_mqtt.h"
#endif
#ifdef HAL_ENABLE_OTA
#include "hal/network/ota/hal_ota.h"
#endif
#ifdef HAL_ENABLE_CELLULAR_MODEM
#include "hal/modem/hal_modem_at.h"
#endif
#ifdef HAL_ENABLE_A7670
#include "hal/modem/hal_simcom_a76xx.h"
#endif
#ifdef HAL_ENABLE_RTC
#include "hal/rtc/hal_rtc.h"
#endif
#ifdef HAL_ENABLE_THERMOCOUPLE
#include "hal/temperature/hal_thermocouple.h"
#endif
#ifdef HAL_ENABLE_DS18B20
#include "hal/temperature/hal_ds18b20.h"
#endif
#ifdef HAL_ENABLE_DHT
#include "hal/temperature/hal_dht.h"
#endif
#ifdef HAL_ENABLE_BH1750
#include "hal/sensors/hal_bh1750.h"
#endif
#ifdef HAL_ENABLE_ADP5360
#include "hal/power/hal_adp5360.h"
#endif
#ifdef HAL_ENABLE_TSC2007
#include "hal/input/hal_tsc2007.h"
#endif
#ifdef HAL_ENABLE_STMPE610
#include "hal/input/hal_stmpe610.h"
#endif
#ifdef HAL_ENABLE_IRSMALL_DECODER
#include "hal/input/hal_irsmall_decoder.h"
#endif
#ifdef HAL_ENABLE_GPS
#include "hal/gps/hal_gps.h"
#endif
#ifdef HAL_ENABLE_EEPROM
#include "hal/storage/hal_eeprom.h"
#endif
#ifdef HAL_ENABLE_KV
#include "hal/storage/hal_kv.h"
#endif

#pragma once

/* Phase 3 compile/link fixture: every ESP32-S3 backend completed through
 * Phase 3 is selected together. Hardware behavior is deliberately deferred
 * to the separately tracked Phase 3.5 verification campaign. */
#define HAL_ENABLE_APP_TASK1 1
#define HAL_ENABLE_BSD_SOCKETS 1
#define HAL_ENABLE_HTTP_CLIENT 1
#define HAL_ENABLE_HTTP_FILES 1
#define HAL_ENABLE_HTTP_SERVER 1
#define HAL_ENABLE_I2C 1
#define HAL_ENABLE_I2C_10BIT 1
#define HAL_ENABLE_I2C_SLAVE 1
#define HAL_ENABLE_MQTT 1
#define HAL_ENABLE_OTA 1
#define HAL_ENABLE_PCNT 1
#define HAL_ENABLE_PWM_FREQ 1
#define HAL_ENABLE_RGB_LED 1
#define HAL_ENABLE_SPI 1
#define HAL_ENABLE_STACK_GUARD 1
#define HAL_ENABLE_TIME 1
#define HAL_ENABLE_TLS 1
#define HAL_ENABLE_UART 1
#define HAL_ENABLE_WEBSOCKET 1
#define HAL_ENABLE_WIFI 1
#define HAL_ENABLE_WIREGUARD 1

#define HAL_FREERTOS_TASK0_CORE 0
#define HAL_FREERTOS_TASK1_CORE 1
#define HAL_FREERTOS_TASK0_STACK 8192u
#define HAL_FREERTOS_TASK1_STACK 8192u

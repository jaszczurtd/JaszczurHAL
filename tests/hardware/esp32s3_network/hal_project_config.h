#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Network verification of the native ESP-IDF backend: WiFi lifecycle, the
 * resolver, UDP and TCP sockets, and the HTTP and WebSocket servers. */
#define HAL_ENABLE_WIFI 1
#define HAL_ENABLE_TCP 1
#define HAL_ENABLE_UDP 1
#define HAL_ENABLE_HTTP_SERVER 1
#define HAL_ENABLE_WEBSOCKET 1
#define HAL_ENABLE_RGB_LED 1

#define HAL_FREERTOS_TASK0_CORE 0
#define HAL_FREERTOS_TASK0_STACK 8192u

#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_RP2350_RISCV)                                                   \
  X(HAL_TARGET_STM32G474)                                                      \
  X(HAL_TARGET_ESP32_S3)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(NETWORK, "FreeRTOS network services and client examples",                  \
    EXAMPLE_FREERTOS_NETWORK = 1)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#ifndef HAL_ENABLE_FREERTOS
#define HAL_ENABLE_FREERTOS
#endif

#ifndef HAL_ENABLE_APP_TASK1
#define HAL_ENABLE_APP_TASK1
#endif

#if defined(EXAMPLE_FREERTOS_NETWORK)
#if defined(HAL_TARGET_RP2350_RISCV)
#error "18_freertos_suite NETWORK: RP2350 RISC-V has no CYW43 board"
#endif
#define HAL_ENABLE_BSD_SOCKETS
#define HAL_ENABLE_CJSON
#define HAL_ENABLE_HTTP_CLIENT
#define HAL_ENABLE_HTTP_FILES
#define HAL_ENABLE_HTTP_SERVER
#define HAL_ENABLE_NET_COMMANDS
#define HAL_ENABLE_NET_CONSOLE
#define HAL_ENABLE_NOTIFY_TELEGRAM
#define HAL_ENABLE_TIME
#define HAL_ENABLE_TLS
#define HAL_ENABLE_WEBSOCKET
#define HAL_ENABLE_WIFI
#define HAL_FREERTOS_TASK0_STACK 1536
#define HAL_FREERTOS_TASK1_STACK 384
#define HAL_HTTP_SERVER_MAX_CLIENTS 1
#define HAL_NET_CONSOLE_MAX_CLIENTS 1
#define HAL_TCP_LISTENER_MAX_INSTANCES 4
#define HAL_TCP_LISTENER_BACKLOG_MAX 2
#define HAL_TCP_SOCKET_MAX_INSTANCES 6
#define HAL_TLS_MAX_CLIENTS 1
#define HAL_WEBSOCKET_MAX_CLIENTS 1
#endif

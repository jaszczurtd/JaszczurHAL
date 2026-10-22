#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(SERVICES, "NTP, TLS, HTTP/HTTPS and MQTT clients and WireGuard",           \
    JH_NET_TEST_SERVICES = 1)

/* Basic suite: WiFi lifecycle of the native ESP-IDF backend, the resolver,
 * UDP and TCP sockets from both application tasks, the native BSD API, and
 * the HTTP (routes, files, network commands), WebSocket and net console
 * servers, IPv6 included. */
#define HAL_ENABLE_WIFI 1
#define HAL_ENABLE_TCP 1
#define HAL_ENABLE_UDP 1
#define HAL_ENABLE_RGB_LED 1
#if !defined(JH_NET_TEST_SERVICES)
#define HAL_ENABLE_APP_TASK1 1
#define HAL_ENABLE_BSD_SOCKETS 1
#define HAL_ENABLE_HTTP_SERVER 1
#define HAL_ENABLE_HTTP_FILES 1
#define HAL_ENABLE_WEBSOCKET 1
#define HAL_ENABLE_NET_COMMANDS 1
#define HAL_ENABLE_NET_CONSOLE 1
/* Every server runs at once: HTTP, WebSocket, console and IPv6 listeners,
 * two clients each, the IPv6 echo client and one refused with "busy". With
 * the UDP sockets they fill 15 of lwIP's 16 descriptors only while every
 * server is busy. */
#define HAL_TCP_LISTENER_MAX_INSTANCES 4u
#define HAL_TCP_SOCKET_MAX_INSTANCES 8u
#define HAL_UDP_SOCKET_MAX_INSTANCES 3u
#define HAL_HTTP_SERVER_MAX_ROUTES 12u
#define HAL_FREERTOS_TASK1_CORE 1
#define HAL_FREERTOS_TASK1_STACK 8192u
#endif

/* SERVICES suite: the clients and the tunnel against the host fixture. The
 * fixture's NTP server listens on an unprivileged port. */
#if defined(JH_NET_TEST_SERVICES)
#define HAL_ENABLE_TIME 1
#define HAL_ENABLE_TLS 1
#define HAL_ENABLE_HTTP_CLIENT 1
#define HAL_ENABLE_MQTT 1
#define HAL_ENABLE_WIREGUARD 1
#define HAL_TIME_NTP_PORT 10123u
#endif

/* Stack overflows of any task, lwIP's included, stop at a watchpoint. */
#define HAL_ENABLE_STACK_GUARD 1

#define HAL_FREERTOS_TASK0_CORE 0
#define HAL_FREERTOS_TASK0_STACK 8192u

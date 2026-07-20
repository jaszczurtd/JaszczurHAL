#pragma once

struct wireguard_handshake;
/*
 * Pico/RP2040 portability layer for the upstream WireGuard (lwIP) port.
 *
 * Notes:
 * - Keep this header C-friendly.
 * - Avoid pulling in ESP-IDF / FreeRTOS dependencies.
 */

#include <lwip/opt.h>

#include "../../../../../hal_serial.h"
#include "../../../../../hal_system.h"

#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

// ---- Logging (ESP-IDF style -> JaszczurHAL serial backend) ----
#ifndef TAG
#define TAG "[WG] "
#endif

#ifndef log_v
#define log_v(...) hal_deb("[V] " __VA_ARGS__)
#endif
#ifndef log_d
#define log_d(...) hal_deb("[D] " __VA_ARGS__)
#endif
#ifndef log_i
#define log_i(...) hal_deb("[I] " __VA_ARGS__)
#endif
#ifndef log_w
#define log_w(...) hal_deb("[W] " __VA_ARGS__)
#endif
#ifndef log_e
#define log_e(...) hal_derr("[E] " __VA_ARGS__)
#endif

#define ESP_LOGV(tag, fmt, ...) hal_deb("[V] %s: " fmt, tag, ##__VA_ARGS__)

// ---- Minimal FreeRTOS compatibility (used by original code) ----
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif
#ifndef vTaskDelay
#define vTaskDelay(ms_ticks) hal_delay_ms((uint32_t)(ms_ticks))
#endif

// ---- Helpers ----
#ifndef WG_IP4_U32
#define WG_IP4_U32(ip4) (ip4_addr_get_u32((const ip4_addr_t *)(ip4)))
#endif

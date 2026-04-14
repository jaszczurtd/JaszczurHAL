#pragma once

#include "hal_config.h"
#ifndef HAL_DISABLE_WIFI

/**
 * @file hal_wifi.h
 * @brief Thread-safe HAL wrapper for WiFi operations.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	HAL_WIFI_MODE_OFF = 0,
	HAL_WIFI_MODE_STA = 1,
	HAL_WIFI_MODE_AP = 2,
	HAL_WIFI_MODE_AP_STA = 3
} hal_wifi_mode_t;

/** @brief Set WiFi mode. */
bool hal_wifi_set_mode(hal_wifi_mode_t mode);

/**
 * @brief Disconnect from WiFi.
 * @param erase_credentials Forwarded to platform implementation.
 */
bool hal_wifi_disconnect(bool erase_credentials);

/**
 * @brief Set station hostname.
 * @param hostname Null-terminated hostname string.
 */
bool hal_wifi_set_hostname(const char *hostname);

/**
 * @brief Start station connection.
 * @param ssid WiFi SSID.
 * @param password WiFi password.
 * @param non_blocking true uses non-blocking begin when available.
 */
bool hal_wifi_begin_station(const char *ssid, const char *password, bool non_blocking);

/** @brief Set socket timeout in milliseconds for WiFi stack operations. */
bool hal_wifi_set_timeout_ms(uint32_t timeout_ms);

/** @brief Return true when station is connected. */
bool hal_wifi_is_connected(void);

/** @brief Return platform-native WiFi status code. */
int hal_wifi_status(void);

/** @brief Return true when local IP address is valid/non-zero. */
bool hal_wifi_has_local_ip(void);

/** @brief Return RSSI in dBm. */
int32_t hal_wifi_rssi(void);

/** @brief Return WiFi signal strength as bars in range 0..5. */
int hal_wifi_get_strength(void);

/**
 * @brief Write local IP to caller buffer.
 * @param out Destination buffer.
 * @param out_size Destination size in bytes.
 */
bool hal_wifi_get_local_ip(char *out, size_t out_size);

/**
 * @brief Write DNS IP to caller buffer.
 * @param out Destination buffer.
 * @param out_size Destination size in bytes.
 */
bool hal_wifi_get_dns_ip(char *out, size_t out_size);

/**
 * @brief Write MAC address to caller buffer.
 * @param out Destination buffer.
 * @param out_size Destination size in bytes.
 */
bool hal_wifi_get_mac(char *out, size_t out_size);

/**
 * @brief Send ICMP ping.
 * @param host_or_ip Hostname or dotted-quad IP.
 * @return >=0 on success, <0 on error.
 */
int hal_wifi_ping(const char *host_or_ip);


#endif /* HAL_DISABLE_WIFI */
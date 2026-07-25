#pragma once

#include "hal_config.h"
#ifdef HAL_ENABLE_WIFI

/**
 * @file hal_wifi.h
 * @brief Thread-safe HAL wrapper for WiFi operations.
 *
 * On RP CYW43 backends, status-returning operations report
 * HAL_EUNSUPPORTED when the board profile has no required radio hardware,
 * HAL_EUNINIT before successful initialization, and HAL_EHW after a failed
 * probe or initialization. Station mode and station join are initialization
 * entry points; state queries and scans never initialize the radio implicitly.
 */

#include "hal_status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_WIFI_SSID_MAX_LEN
#define HAL_WIFI_SSID_MAX_LEN 33u
#endif

#ifndef HAL_WIFI_BSSID_LEN
#define HAL_WIFI_BSSID_LEN 6u
#endif

typedef enum {
  HAL_WIFI_MODE_OFF = 0,
  HAL_WIFI_MODE_STA = 1,
  HAL_WIFI_MODE_AP = 2,
  HAL_WIFI_MODE_AP_STA = 3
} hal_wifi_mode_t;

/** Backend-neutral station/link state. */
typedef enum {
  HAL_WIFI_STATE_OFF = 0,
  HAL_WIFI_STATE_IDLE,
  HAL_WIFI_STATE_CONNECTING,
  HAL_WIFI_STATE_CONNECTED_NO_IP,
  HAL_WIFI_STATE_CONNECTED,
  HAL_WIFI_STATE_NO_NETWORK,
  HAL_WIFI_STATE_AUTH_FAILED,
  HAL_WIFI_STATE_FAILED
} hal_wifi_state_t;

typedef enum {
  HAL_WIFI_ENC_UNKNOWN = 0,
  HAL_WIFI_ENC_NONE,
  HAL_WIFI_ENC_WPA,
  HAL_WIFI_ENC_WPA2,
  HAL_WIFI_ENC_AUTO
} hal_wifi_encryption_t;

typedef struct {
  char ssid[HAL_WIFI_SSID_MAX_LEN];
  uint8_t bssid[HAL_WIFI_BSSID_LEN];
  hal_wifi_encryption_t encryption;
  int32_t rssi;
  int32_t channel;
} hal_wifi_scan_result_t;

hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode);
hal_status_t hal_wifi_disconnect_ex(bool erase_credentials);
hal_status_t hal_wifi_set_hostname_ex(const char *hostname);
hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking);
hal_status_t hal_wifi_set_timeout_ms_ex(uint32_t timeout_ms);
hal_status_t hal_wifi_get_state_ex(hal_wifi_state_t *out_state);
hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size);
hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result);
hal_status_t hal_wifi_scan_networks_ex(int *out_count);
hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out);

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
 * @param non_blocking true starts the join and returns after the backend
 * accepts the request; query the connection state separately.
 */
bool hal_wifi_begin_station(const char *ssid, const char *password,
                            bool non_blocking);

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

/**
 * @brief Send ICMP ping with per-call timeout.
 * @param host_or_ip Hostname or dotted-quad IP.
 * @param timeout_ms Timeout in milliseconds for this call only.
 * @return >=0 on success, <0 on error.
 */
int hal_wifi_ping_ex(const char *host_or_ip, uint32_t timeout_ms);

/**
 * @brief Scan nearby WiFi networks.
 * @return Number of networks found on success; negative value on error.
 */
int hal_wifi_scan_networks(void);

/**
 * @brief Copy one result from the last hal_wifi_scan_networks() call.
 * @param index Zero-based result index.
 * @param out Destination result struct.
 * @return true when @p out was filled; false for invalid index/argument.
 */
bool hal_wifi_get_scan_result(size_t index, hal_wifi_scan_result_t *out);

/** @brief Convert a HAL WiFi encryption value to a short printable label. */
const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_WIFI */

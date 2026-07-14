#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_serial.h"
#include "../../hal_wifi.h"
#include "hal_mock.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool s_connected = false;
static int s_status = 0;
static int32_t s_rssi = -100;
static hal_wifi_mode_t s_mode = HAL_WIFI_MODE_OFF;
static bool s_has_local_ip = false;
static char s_ip[32] = "0.0.0.0";
static char s_dns[32] = "0.0.0.0";
static char s_mac[32] = "00:00:00:00:00:00";
static char s_hostname[64] = "";
static uint32_t s_timeout_ms = 0;
static int s_ping_result = -1;

#ifndef HAL_MOCK_WIFI_MAX_SCAN_RESULTS
#define HAL_MOCK_WIFI_MAX_SCAN_RESULTS 8u
#endif

static hal_wifi_scan_result_t s_scan_results[HAL_MOCK_WIFI_MAX_SCAN_RESULTS];
static size_t s_scan_count = 0;

static bool validate_out(char *out, size_t out_size, const char *fn) {
  if (!out) {
    hal_derr("%s: output buffer is NULL", fn);
    return false;
  }
  if (out_size == 0) {
    hal_derr("%s: output buffer size is 0", fn);
    return false;
  }
  return true;
}

static bool checked_snprintf(char *out, size_t out_size, const char *fn,
                             const char *format, ...) {
  va_list args;
  va_start(args, format);
  int written = vsnprintf(out, out_size, format, args);
  va_end(args);

  if (written < 0) {
    hal_derr("%s: snprintf failed", fn);
    return false;
  }
  if ((size_t)written >= out_size) {
    hal_derr("%s: output buffer too small", fn);
    return false;
  }
  return true;
}

hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode) {
  switch (mode) {
  case HAL_WIFI_MODE_OFF:
  case HAL_WIFI_MODE_STA:
  case HAL_WIFI_MODE_AP:
  case HAL_WIFI_MODE_AP_STA:
    s_mode = mode;
    return HAL_OK;
  default:
    hal_derr("hal_wifi_set_mode: invalid mode value %d", (int)mode);
    return HAL_EINVAL;
  }
}

bool hal_wifi_set_mode(hal_wifi_mode_t mode) {
  return hal_status_to_bool(hal_wifi_set_mode_ex(mode));
}

hal_status_t hal_wifi_disconnect_ex(bool erase_credentials) {
  (void)erase_credentials;
  s_connected = false;
  s_status = 0;
  s_has_local_ip = false;
  snprintf(s_ip, sizeof(s_ip), "%s", "0.0.0.0");
  return HAL_OK;
}

bool hal_wifi_disconnect(bool erase_credentials) {
  return hal_status_to_bool(hal_wifi_disconnect_ex(erase_credentials));
}

hal_status_t hal_wifi_set_hostname_ex(const char *hostname) {
  if (!hostname || hostname[0] == '\0') {
    hal_derr("hal_wifi_set_hostname: hostname is NULL/empty");
    return HAL_EINVAL;
  }
  snprintf(s_hostname, sizeof(s_hostname), "%s", hostname);
  return HAL_OK;
}

bool hal_wifi_set_hostname(const char *hostname) {
  return hal_status_to_bool(hal_wifi_set_hostname_ex(hostname));
}

hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking) {
  (void)non_blocking;
  if (!ssid || ssid[0] == '\0') {
    hal_derr("hal_wifi_begin_station: SSID is NULL/empty");
    return HAL_EINVAL;
  }
  if (!password) {
    hal_derr("hal_wifi_begin_station: password pointer is NULL");
    return HAL_EINVAL;
  }
  return HAL_OK;
}

bool hal_wifi_begin_station(const char *ssid, const char *password,
                            bool non_blocking) {
  return hal_status_to_bool(
      hal_wifi_begin_station_ex(ssid, password, non_blocking));
}

hal_status_t hal_wifi_set_timeout_ms_ex(uint32_t timeout_ms) {
  s_timeout_ms = timeout_ms;
  return HAL_OK;
}

bool hal_wifi_set_timeout_ms(uint32_t timeout_ms) {
  return hal_status_to_bool(hal_wifi_set_timeout_ms_ex(timeout_ms));
}

bool hal_wifi_is_connected(void) { return s_connected; }

int hal_wifi_status(void) { return s_status; }

bool hal_wifi_has_local_ip(void) { return s_has_local_ip; }

int32_t hal_wifi_rssi(void) { return s_rssi; }

int hal_wifi_get_strength(void) {
  if (s_rssi >= 0)
    return 0;
  if (s_rssi >= -50)
    return 5;
  if (s_rssi >= -60)
    return 4;
  if (s_rssi >= -70)
    return 3;
  if (s_rssi >= -80)
    return 2;
  if (s_rssi >= -90)
    return 1;
  return 0;
}

hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size) {
  if (!validate_out(out, out_size, "hal_wifi_get_local_ip"))
    return HAL_EINVAL;
  return checked_snprintf(out, out_size, "hal_wifi_get_local_ip", "%s", s_ip)
             ? HAL_OK
             : HAL_EOVERFLOW;
}

bool hal_wifi_get_local_ip(char *out, size_t out_size) {
  return hal_status_to_bool(hal_wifi_get_local_ip_ex(out, out_size));
}

hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size) {
  if (!validate_out(out, out_size, "hal_wifi_get_dns_ip"))
    return HAL_EINVAL;
  return checked_snprintf(out, out_size, "hal_wifi_get_dns_ip", "%s", s_dns)
             ? HAL_OK
             : HAL_EOVERFLOW;
}

bool hal_wifi_get_dns_ip(char *out, size_t out_size) {
  return hal_status_to_bool(hal_wifi_get_dns_ip_ex(out, out_size));
}

hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size) {
  if (!validate_out(out, out_size, "hal_wifi_get_mac"))
    return HAL_EINVAL;
  return checked_snprintf(out, out_size, "hal_wifi_get_mac", "%s", s_mac)
             ? HAL_OK
             : HAL_EOVERFLOW;
}

bool hal_wifi_get_mac(char *out, size_t out_size) {
  return hal_status_to_bool(hal_wifi_get_mac_ex(out, out_size));
}

int hal_wifi_ping(const char *host_or_ip) {
  int result = -1;
  (void)hal_wifi_ping_status_ex(host_or_ip, s_timeout_ms, &result);
  return result;
}

hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result) {
  if (out_result != NULL) {
    *out_result = -1;
  }
  if (!host_or_ip || host_or_ip[0] == '\0') {
    hal_derr("hal_wifi_ping_ex: host_or_ip is NULL/empty");
    return HAL_EINVAL;
  }
  if (out_result == NULL) {
    return HAL_EINVAL;
  }

  const uint32_t previous_timeout_ms = s_timeout_ms;
  s_timeout_ms = timeout_ms;
  *out_result = s_ping_result;
  s_timeout_ms = previous_timeout_ms;
  return *out_result >= 0 ? HAL_OK : HAL_EIO;
}

int hal_wifi_ping_ex(const char *host_or_ip, uint32_t timeout_ms) {
  int result = -1;
  (void)hal_wifi_ping_status_ex(host_or_ip, timeout_ms, &result);
  return result;
}

hal_status_t hal_wifi_scan_networks_ex(int *out_count) {
  if (out_count == NULL) {
    return HAL_EINVAL;
  }
  *out_count = (int)s_scan_count;
  return HAL_OK;
}

int hal_wifi_scan_networks(void) {
  int count = -1;
  (void)hal_wifi_scan_networks_ex(&count);
  return count;
}

hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out) {
  if (out == NULL) {
    hal_derr("hal_wifi_get_scan_result: output pointer is NULL");
    return HAL_EINVAL;
  }
  if (index >= s_scan_count) {
    hal_derr("hal_wifi_get_scan_result: index %u out of range",
             (unsigned)index);
    return HAL_ENOENT;
  }
  *out = s_scan_results[index];
  return HAL_OK;
}

bool hal_wifi_get_scan_result(size_t index, hal_wifi_scan_result_t *out) {
  return hal_status_to_bool(hal_wifi_get_scan_result_ex(index, out));
}

const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption) {
  switch (encryption) {
  case HAL_WIFI_ENC_NONE:
    return "NONE";
  case HAL_WIFI_ENC_WPA:
    return "WPA";
  case HAL_WIFI_ENC_WPA2:
    return "WPA2";
  case HAL_WIFI_ENC_AUTO:
    return "AUTO";
  case HAL_WIFI_ENC_UNKNOWN:
  default:
    return "UNKN";
  }
}

void hal_mock_wifi_reset(void) {
  s_connected = false;
  s_status = 0;
  s_rssi = -100;
  s_mode = HAL_WIFI_MODE_OFF;
  s_has_local_ip = false;
  s_ping_result = -1;
  s_timeout_ms = 0;
  s_hostname[0] = '\0';
  s_scan_count = 0;
  memset(s_scan_results, 0, sizeof(s_scan_results));
  snprintf(s_ip, sizeof(s_ip), "%s", "0.0.0.0");
  snprintf(s_dns, sizeof(s_dns), "%s", "0.0.0.0");
  snprintf(s_mac, sizeof(s_mac), "%s", "00:00:00:00:00:00");
}

void hal_mock_wifi_set_connected(bool connected) { s_connected = connected; }

void hal_mock_wifi_set_status(int status) { s_status = status; }

void hal_mock_wifi_set_rssi(int32_t rssi) { s_rssi = rssi; }

void hal_mock_wifi_set_local_ip(const char *ip) {
  snprintf(s_ip, sizeof(s_ip), "%s", ip ? ip : "0.0.0.0");
  s_has_local_ip = (strcmp(s_ip, "0.0.0.0") != 0);
}

void hal_mock_wifi_set_dns_ip(const char *ip) {
  snprintf(s_dns, sizeof(s_dns), "%s", ip ? ip : "0.0.0.0");
}

void hal_mock_wifi_set_mac(const char *mac) {
  snprintf(s_mac, sizeof(s_mac), "%s", mac ? mac : "00:00:00:00:00:00");
}

void hal_mock_wifi_set_ping_result(int result) { s_ping_result = result; }

const char *hal_mock_wifi_get_hostname(void) { return s_hostname; }

uint32_t hal_mock_wifi_get_timeout_ms(void) { return s_timeout_ms; }

bool hal_mock_wifi_set_scan_result(size_t index, const char *ssid,
                                   hal_wifi_encryption_t encryption,
                                   const uint8_t bssid[HAL_WIFI_BSSID_LEN],
                                   int32_t channel, int32_t rssi) {
  if (index >= HAL_MOCK_WIFI_MAX_SCAN_RESULTS) {
    return false;
  }

  hal_wifi_scan_result_t *out = &s_scan_results[index];
  memset(out, 0, sizeof(*out));
  snprintf(out->ssid, sizeof(out->ssid), "%s", ssid ? ssid : "");
  if (bssid != NULL) {
    memcpy(out->bssid, bssid, HAL_WIFI_BSSID_LEN);
  }
  out->encryption = encryption;
  out->channel = channel;
  out->rssi = rssi;
  if (s_scan_count <= index) {
    s_scan_count = index + 1u;
  }
  return true;
}
#endif // HAL_TARGET_IS_MOCK

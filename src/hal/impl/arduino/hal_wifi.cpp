#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_WIFI

#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_wifi.h"
#include "../shared/hal_mutex_once.h"
#include <WiFi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static hal_mutex_t s_wifi_mutex = NULL;
static uint32_t s_wifi_timeout_ms = 15000u;
static int s_wifi_scan_count = 0;

static inline void wifi_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_wifi_mutex);
}

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

static bool format_ipv4(char *out, size_t out_size, const char *fn,
                        const IPAddress &ip) {
  return checked_snprintf(out, out_size, fn, "%u.%u.%u.%u", (unsigned)ip[0],
                          (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
}

static bool format_mac(char *out, size_t out_size, const char *fn,
                       const uint8_t mac[6]) {
  return checked_snprintf(out, out_size, fn, "%02X:%02X:%02X:%02X:%02X:%02X",
                          (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
                          (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
}

static hal_wifi_encryption_t map_encryption(uint8_t enc) {
  switch (enc) {
  case ENC_TYPE_NONE:
    return HAL_WIFI_ENC_NONE;
  case ENC_TYPE_TKIP:
    return HAL_WIFI_ENC_WPA;
  case ENC_TYPE_CCMP:
    return HAL_WIFI_ENC_WPA2;
  case ENC_TYPE_AUTO:
    return HAL_WIFI_ENC_AUTO;
  default:
    return HAL_WIFI_ENC_UNKNOWN;
  }
}

bool hal_wifi_set_mode(hal_wifi_mode_t mode) {
  WiFiMode_t platform_mode;
  switch (mode) {
  case HAL_WIFI_MODE_OFF:
    platform_mode = WIFI_OFF;
    break;
  case HAL_WIFI_MODE_STA:
    platform_mode = WIFI_STA;
    break;
  case HAL_WIFI_MODE_AP:
    platform_mode = WIFI_AP;
    break;
  case HAL_WIFI_MODE_AP_STA:
    platform_mode = WIFI_AP_STA;
    break;
  default:
    hal_derr("hal_wifi_set_mode: invalid mode value %d", (int)mode);
    return false;
  }

  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  WiFi.mode(platform_mode);
  hal_mutex_unlock(s_wifi_mutex);
  return true;
}

bool hal_wifi_disconnect(bool erase_credentials) {
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  WiFi.disconnect(erase_credentials);
  hal_mutex_unlock(s_wifi_mutex);
  return true;
}

bool hal_wifi_set_hostname(const char *hostname) {
  if (!hostname || hostname[0] == '\0') {
    hal_derr("hal_wifi_set_hostname: hostname is NULL/empty");
    return false;
  }

  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  WiFi.setHostname(hostname);
  hal_mutex_unlock(s_wifi_mutex);
  return true;
}

bool hal_wifi_begin_station(const char *ssid, const char *password,
                            bool non_blocking) {
  if (!ssid || ssid[0] == '\0') {
    hal_derr("hal_wifi_begin_station: SSID is NULL/empty");
    return false;
  }
  if (!password) {
    hal_derr("hal_wifi_begin_station: password pointer is NULL");
    return false;
  }

  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  if (non_blocking) {
    WiFi.beginNoBlock(ssid, password);
  } else {
    WiFi.begin(ssid, password);
  }
  hal_mutex_unlock(s_wifi_mutex);
  return true;
}

bool hal_wifi_set_timeout_ms(uint32_t timeout_ms) {
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  s_wifi_timeout_ms = timeout_ms;
  WiFi.setTimeout(timeout_ms);
  hal_mutex_unlock(s_wifi_mutex);
  return true;
}

bool hal_wifi_is_connected(void) {
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  const bool connected = (WiFi.status() == WL_CONNECTED);
  hal_mutex_unlock(s_wifi_mutex);
  return connected;
}

int hal_wifi_status(void) {
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  const int st = (int)WiFi.status();
  hal_mutex_unlock(s_wifi_mutex);
  return st;
}

bool hal_wifi_has_local_ip(void) {
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  const bool has_ip = (WiFi.localIP() != IPAddress(0, 0, 0, 0));
  hal_mutex_unlock(s_wifi_mutex);
  return has_ip;
}

int32_t hal_wifi_rssi(void) {
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  const int32_t rssi = WiFi.RSSI();
  hal_mutex_unlock(s_wifi_mutex);
  return rssi;
}

int hal_wifi_get_strength(void) {
  const int32_t rssi = hal_wifi_rssi();

  if (rssi >= 0)
    return 0;
  if (rssi >= -50)
    return 5;
  if (rssi >= -60)
    return 4;
  if (rssi >= -70)
    return 3;
  if (rssi >= -80)
    return 2;
  if (rssi >= -90)
    return 1;
  return 0;
}

bool hal_wifi_get_local_ip(char *out, size_t out_size) {
  if (!validate_out(out, out_size, "hal_wifi_get_local_ip"))
    return false;

  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  IPAddress ip = WiFi.localIP();
  hal_mutex_unlock(s_wifi_mutex);

  return format_ipv4(out, out_size, "hal_wifi_get_local_ip", ip);
}

bool hal_wifi_get_dns_ip(char *out, size_t out_size) {
  if (!validate_out(out, out_size, "hal_wifi_get_dns_ip"))
    return false;

  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  IPAddress ip = WiFi.dnsIP();
  hal_mutex_unlock(s_wifi_mutex);

  return format_ipv4(out, out_size, "hal_wifi_get_dns_ip", ip);
}

bool hal_wifi_get_mac(char *out, size_t out_size) {
  if (!validate_out(out, out_size, "hal_wifi_get_mac"))
    return false;

  uint8_t mac[6] = {0};
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  WiFi.macAddress(mac);
  hal_mutex_unlock(s_wifi_mutex);

  return format_mac(out, out_size, "hal_wifi_get_mac", mac);
}

int hal_wifi_ping(const char *host_or_ip) {
  return hal_wifi_ping_ex(host_or_ip, s_wifi_timeout_ms);
}

int hal_wifi_ping_ex(const char *host_or_ip, uint32_t timeout_ms) {
  if (!host_or_ip || host_or_ip[0] == '\0') {
    hal_derr("hal_wifi_ping_ex: host_or_ip is NULL/empty");
    return -1;
  }

  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  const uint32_t previous_timeout_ms = s_wifi_timeout_ms;
  if (timeout_ms != previous_timeout_ms) {
    WiFi.setTimeout(timeout_ms);
  }
  const int res = WiFi.ping(host_or_ip);
  if (timeout_ms != previous_timeout_ms) {
    WiFi.setTimeout(previous_timeout_ms);
  }
  hal_mutex_unlock(s_wifi_mutex);
  return res;
}

int hal_wifi_scan_networks(void) {
  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  s_wifi_scan_count = WiFi.scanNetworks();
  hal_mutex_unlock(s_wifi_mutex);
  return s_wifi_scan_count;
}

bool hal_wifi_get_scan_result(size_t index, hal_wifi_scan_result_t *out) {
  if (out == NULL) {
    hal_derr("hal_wifi_get_scan_result: output pointer is NULL");
    return false;
  }

  wifi_ensure_mutex();
  hal_mutex_lock(s_wifi_mutex);
  if (s_wifi_scan_count <= 0 || index >= (size_t)s_wifi_scan_count) {
    hal_mutex_unlock(s_wifi_mutex);
    hal_derr("hal_wifi_get_scan_result: index %u out of range",
             (unsigned)index);
    return false;
  }

  bool ssid_ok =
      checked_snprintf(out->ssid, sizeof(out->ssid), "hal_wifi_get_scan_result",
                       "%s", WiFi.SSID((int)index).c_str());
  WiFi.BSSID((int)index, out->bssid);
  out->encryption = map_encryption((uint8_t)WiFi.encryptionType((int)index));
  out->channel = (int32_t)WiFi.channel((int)index);
  out->rssi = (int32_t)WiFi.RSSI((int)index);
  hal_mutex_unlock(s_wifi_mutex);
  return ssid_ok;
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

#endif /* HAL_ENABLE_WIFI */
#endif // HAL_TARGET_IS_RP2040

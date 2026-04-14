#include "../../hal_config.h"
#ifndef HAL_DISABLE_WIFI

#include "../../hal_wifi.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include <WiFi.h>
#include <string.h>
#include <stdio.h>

static hal_mutex_t s_wifi_mutex = NULL;

static inline void wifi_ensure_mutex(void) {
    if (s_wifi_mutex == NULL) {
        hal_critical_section_enter();
        if (s_wifi_mutex == NULL) {
            s_wifi_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
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

bool hal_wifi_begin_station(const char *ssid, const char *password, bool non_blocking) {
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

    if (rssi >= 0) return 0;
    if (rssi >= -50) return 5;
    if (rssi >= -60) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -80) return 2;
    if (rssi >= -90) return 1;
    return 0;
}

bool hal_wifi_get_local_ip(char *out, size_t out_size) {
    if (!validate_out(out, out_size, "hal_wifi_get_local_ip")) return false;

    wifi_ensure_mutex();
    hal_mutex_lock(s_wifi_mutex);
    String ip = WiFi.localIP().toString();
    hal_mutex_unlock(s_wifi_mutex);

    if (snprintf(out, out_size, "%s", ip.c_str()) < 0) {
        hal_derr("hal_wifi_get_local_ip: snprintf failed");
        return false;
    }
    return true;
}

bool hal_wifi_get_dns_ip(char *out, size_t out_size) {
    if (!validate_out(out, out_size, "hal_wifi_get_dns_ip")) return false;

    wifi_ensure_mutex();
    hal_mutex_lock(s_wifi_mutex);
    String ip = WiFi.dnsIP().toString();
    hal_mutex_unlock(s_wifi_mutex);

    if (snprintf(out, out_size, "%s", ip.c_str()) < 0) {
        hal_derr("hal_wifi_get_dns_ip: snprintf failed");
        return false;
    }
    return true;
}

bool hal_wifi_get_mac(char *out, size_t out_size) {
    if (!validate_out(out, out_size, "hal_wifi_get_mac")) return false;

    wifi_ensure_mutex();
    hal_mutex_lock(s_wifi_mutex);
    String mac = WiFi.macAddress();
    hal_mutex_unlock(s_wifi_mutex);

    if (snprintf(out, out_size, "%s", mac.c_str()) < 0) {
        hal_derr("hal_wifi_get_mac: snprintf failed");
        return false;
    }
    return true;
}

int hal_wifi_ping(const char *host_or_ip) {
    if (!host_or_ip || host_or_ip[0] == '\0') {
        hal_derr("hal_wifi_ping: host_or_ip is NULL/empty");
        return -1;
    }

    wifi_ensure_mutex();
    hal_mutex_lock(s_wifi_mutex);
    const int res = WiFi.ping(host_or_ip);
    hal_mutex_unlock(s_wifi_mutex);
    return res;
}


#endif /* HAL_DISABLE_WIFI */

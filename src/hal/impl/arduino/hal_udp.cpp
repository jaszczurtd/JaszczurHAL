#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_UDP

#include "../../hal_udp.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"

#include <WiFiUdp.h>
#include <IPAddress.h>
#include <stdio.h>
#include <string.h>

static WiFiUDP s_udp;
static hal_mutex_t s_udp_mutex = NULL;
static bool s_udp_started = false;
static bool s_packet_started = false;
static IPAddress s_last_remote_ip(0, 0, 0, 0);
static uint16_t s_last_remote_port = 0u;

static inline void udp_ensure_mutex(void) {
    if (s_udp_mutex == NULL) {
        hal_critical_section_enter();
        if (s_udp_mutex == NULL) {
            s_udp_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

static bool validate_out(char *out, size_t out_size, const char *fn) {
    if (!out) {
        hal_derr("%s: output buffer is NULL", fn);
        return false;
    }
    if (out_size == 0u) {
        hal_derr("%s: output buffer size is 0", fn);
        return false;
    }
    return true;
}

static bool validate_non_empty(const char *value, const char *fn, const char *name) {
    if (!value || value[0] == '\0') {
        hal_derr("%s: %s is NULL/empty", fn, name);
        return false;
    }
    return true;
}

static bool ip_is_zero(const IPAddress &ip) {
    return ip[0] == 0u && ip[1] == 0u && ip[2] == 0u && ip[3] == 0u;
}

static void reset_last_remote(void) {
    s_last_remote_ip = IPAddress(0, 0, 0, 0);
    s_last_remote_port = 0u;
}

bool hal_udp_begin(uint16_t local_port) {
    if (local_port == 0u) {
        hal_derr("hal_udp_begin: local_port must be > 0");
        return false;
    }

    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    const bool ok = s_udp.begin(local_port);
    if (ok) {
        s_udp_started = true;
        s_packet_started = false;
        reset_last_remote();
    }

    hal_mutex_unlock(s_udp_mutex);

    if (!ok) {
        hal_derr("hal_udp_begin: begin(%u) failed", (unsigned)local_port);
    }
    return ok;
}

void hal_udp_stop(void) {
    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    s_udp.stop();
    s_udp_started = false;
    s_packet_started = false;
    reset_last_remote();

    hal_mutex_unlock(s_udp_mutex);
}

int hal_udp_parse_packet(void) {
    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    if (!s_udp_started) {
        hal_mutex_unlock(s_udp_mutex);
        return 0;
    }

    const int packet_size = s_udp.parsePacket();
    if (packet_size > 0) {
        s_last_remote_ip = s_udp.remoteIP();
        s_last_remote_port = s_udp.remotePort();
    }

    hal_mutex_unlock(s_udp_mutex);
    return packet_size;
}

int hal_udp_read(uint8_t *buffer, uint16_t max_len) {
    if (max_len > 0u && buffer == NULL) {
        hal_derr("hal_udp_read: buffer is NULL while max_len > 0");
        return -1;
    }
    if (max_len == 0u) {
        return 0;
    }

    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    if (!s_udp_started) {
        hal_mutex_unlock(s_udp_mutex);
        return 0;
    }

    const int read_count = s_udp.read(buffer, max_len);

    hal_mutex_unlock(s_udp_mutex);
    return read_count;
}

bool hal_udp_remote_ip(char *out, size_t out_size) {
    if (!validate_out(out, out_size, "hal_udp_remote_ip")) {
        return false;
    }

    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    const IPAddress remote_ip = s_last_remote_ip;
    const uint16_t remote_port = s_last_remote_port;

    hal_mutex_unlock(s_udp_mutex);

    if (remote_port == 0u || ip_is_zero(remote_ip)) {
        if (snprintf(out, out_size, "%s", "0.0.0.0") < 0) {
            hal_derr("hal_udp_remote_ip: snprintf failed for empty endpoint");
            return false;
        }
        return false;
    }

    if (snprintf(out,
                 out_size,
                 "%u.%u.%u.%u",
                 (unsigned)remote_ip[0],
                 (unsigned)remote_ip[1],
                 (unsigned)remote_ip[2],
                 (unsigned)remote_ip[3]) < 0) {
        hal_derr("hal_udp_remote_ip: snprintf failed");
        return false;
    }

    return true;
}

uint16_t hal_udp_remote_port(void) {
    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    const uint16_t remote_port = s_last_remote_port;

    hal_mutex_unlock(s_udp_mutex);
    return remote_port;
}

bool hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port) {
    if (!validate_non_empty(host_or_ip, "hal_udp_begin_packet", "host_or_ip")) {
        return false;
    }
    if (remote_port == 0u) {
        hal_derr("hal_udp_begin_packet: remote_port must be > 0");
        return false;
    }

    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    if (!s_udp_started) {
        hal_mutex_unlock(s_udp_mutex);
        hal_derr("hal_udp_begin_packet: UDP socket is not started");
        return false;
    }

    const bool ok = s_udp.beginPacket(host_or_ip, remote_port);
    s_packet_started = ok;

    hal_mutex_unlock(s_udp_mutex);

    if (!ok) {
        hal_derr("hal_udp_begin_packet: beginPacket failed");
    }
    return ok;
}

bool hal_udp_begin_packet_remote(void) {
    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    if (!s_udp_started) {
        hal_mutex_unlock(s_udp_mutex);
        hal_derr("hal_udp_begin_packet_remote: UDP socket is not started");
        return false;
    }

    if (s_last_remote_port == 0u || ip_is_zero(s_last_remote_ip)) {
        hal_mutex_unlock(s_udp_mutex);
        hal_derr("hal_udp_begin_packet_remote: remote endpoint is not available");
        return false;
    }

    const bool ok = s_udp.beginPacket(s_last_remote_ip, s_last_remote_port);
    s_packet_started = ok;

    hal_mutex_unlock(s_udp_mutex);

    if (!ok) {
        hal_derr("hal_udp_begin_packet_remote: beginPacket failed");
    }
    return ok;
}

uint16_t hal_udp_write(const uint8_t *data, uint16_t len) {
    if (len > 0u && data == NULL) {
        hal_derr("hal_udp_write: data is NULL while len > 0");
        return 0u;
    }
    if (len == 0u) {
        return 0u;
    }

    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    if (!s_udp_started || !s_packet_started) {
        hal_mutex_unlock(s_udp_mutex);
        return 0u;
    }

    size_t written = s_udp.write(data, len);
    if (written > 65535u) {
        written = 65535u;
    }

    hal_mutex_unlock(s_udp_mutex);

    return (uint16_t)written;
}

uint16_t hal_udp_write_str(const char *text) {
    if (!text) {
        hal_derr("hal_udp_write_str: text is NULL");
        return 0u;
    }

    const size_t len = strnlen(text, 65535u);
    return hal_udp_write((const uint8_t *)text, (uint16_t)len);
}

bool hal_udp_end_packet(void) {
    udp_ensure_mutex();
    hal_mutex_lock(s_udp_mutex);

    if (!s_udp_started || !s_packet_started) {
        hal_mutex_unlock(s_udp_mutex);
        return false;
    }

    const int rc = s_udp.endPacket();
    s_packet_started = false;

    hal_mutex_unlock(s_udp_mutex);

    if (rc != 1) {
        hal_derr("hal_udp_end_packet: endPacket failed (rc=%d)", rc);
        return false;
    }

    return true;
}

#endif /* HAL_ENABLE_UDP */
#endif  // HAL_TARGET_IS_RP2040

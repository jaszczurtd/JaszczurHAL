#include "../../hal_config.h"

#ifdef HAL_ENABLE_UDP

#include "../../hal_udp.h"
#include "../../hal_serial.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

#define MOCK_UDP_HOST_BUF_SIZE 128u
#define MOCK_UDP_PAYLOAD_BUF_SIZE 512u

static struct {
    bool started;
    bool packet_started;
    uint16_t local_port;

    char last_begin_packet_host[MOCK_UDP_HOST_BUF_SIZE];
    uint16_t last_begin_packet_port;

    char remote_ip[HAL_UDP_IP_STR_LEN];
    uint16_t remote_port;

    uint8_t rx_payload[MOCK_UDP_PAYLOAD_BUF_SIZE];
    uint16_t rx_len;
    uint16_t rx_pos;
    bool rx_pending;

    uint8_t tx_payload[MOCK_UDP_PAYLOAD_BUF_SIZE];
    uint16_t tx_len;
    bool end_packet_called;
    bool end_packet_result;
} s_udp;

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

void hal_mock_udp_reset(void) {
    memset(&s_udp, 0, sizeof(s_udp));
    snprintf(s_udp.remote_ip, sizeof(s_udp.remote_ip), "%s", "0.0.0.0");
    s_udp.end_packet_result = true;
}

bool hal_udp_begin(uint16_t local_port) {
    if (local_port == 0u) {
        hal_derr("hal_udp_begin: local_port must be > 0");
        return false;
    }

    s_udp.started = true;
    s_udp.packet_started = false;
    s_udp.local_port = local_port;
    snprintf(s_udp.remote_ip, sizeof(s_udp.remote_ip), "%s", "0.0.0.0");
    s_udp.remote_port = 0u;
    s_udp.end_packet_called = false;
    s_udp.tx_len = 0u;
    return true;
}

void hal_udp_stop(void) {
    s_udp.started = false;
    s_udp.packet_started = false;
    snprintf(s_udp.remote_ip, sizeof(s_udp.remote_ip), "%s", "0.0.0.0");
    s_udp.remote_port = 0u;
    s_udp.rx_pending = false;
    s_udp.rx_len = 0u;
    s_udp.rx_pos = 0u;
    s_udp.tx_len = 0u;
    s_udp.end_packet_called = false;
}

int hal_udp_parse_packet(void) {
    if (!s_udp.started) {
        return 0;
    }
    return s_udp.rx_pending ? (int)s_udp.rx_len : 0;
}

int hal_udp_read(uint8_t *buffer, uint16_t max_len) {
    if (max_len > 0u && buffer == NULL) {
        hal_derr("hal_udp_read: buffer is NULL while max_len > 0");
        return -1;
    }
    if (!s_udp.started || !s_udp.rx_pending || max_len == 0u) {
        return 0;
    }

    const uint16_t available = (uint16_t)(s_udp.rx_len - s_udp.rx_pos);
    uint16_t to_copy = max_len;
    if (to_copy > available) {
        to_copy = available;
    }

    memcpy(buffer, s_udp.rx_payload + s_udp.rx_pos, to_copy);
    s_udp.rx_pos = (uint16_t)(s_udp.rx_pos + to_copy);

    if (s_udp.rx_pos >= s_udp.rx_len) {
        s_udp.rx_pending = false;
        s_udp.rx_len = 0u;
        s_udp.rx_pos = 0u;
    }

    return (int)to_copy;
}

bool hal_udp_remote_ip(char *out, size_t out_size) {
    if (!validate_out(out, out_size, "hal_udp_remote_ip")) {
        return false;
    }

    if (snprintf(out, out_size, "%s", s_udp.remote_ip) < 0) {
        hal_derr("hal_udp_remote_ip: snprintf failed");
        return false;
    }

    return s_udp.remote_port != 0u;
}

uint16_t hal_udp_remote_port(void) {
    return s_udp.remote_port;
}

bool hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port) {
    if (!validate_non_empty(host_or_ip, "hal_udp_begin_packet", "host_or_ip")) {
        return false;
    }
    if (remote_port == 0u) {
        hal_derr("hal_udp_begin_packet: remote_port must be > 0");
        return false;
    }
    if (!s_udp.started) {
        hal_derr("hal_udp_begin_packet: UDP socket is not started");
        return false;
    }

    snprintf(s_udp.last_begin_packet_host,
             sizeof(s_udp.last_begin_packet_host),
             "%s",
             host_or_ip);
    s_udp.last_begin_packet_port = remote_port;
    s_udp.packet_started = true;
    s_udp.tx_len = 0u;
    s_udp.end_packet_called = false;
    return true;
}

bool hal_udp_begin_packet_remote(void) {
    if (!s_udp.started) {
        hal_derr("hal_udp_begin_packet_remote: UDP socket is not started");
        return false;
    }
    if (s_udp.remote_port == 0u || s_udp.remote_ip[0] == '\0' || strcmp(s_udp.remote_ip, "0.0.0.0") == 0) {
        hal_derr("hal_udp_begin_packet_remote: remote endpoint is not available");
        return false;
    }

    snprintf(s_udp.last_begin_packet_host,
             sizeof(s_udp.last_begin_packet_host),
             "%s",
             s_udp.remote_ip);
    s_udp.last_begin_packet_port = s_udp.remote_port;
    s_udp.packet_started = true;
    s_udp.tx_len = 0u;
    s_udp.end_packet_called = false;
    return true;
}

uint16_t hal_udp_write(const uint8_t *data, uint16_t len) {
    if (len > 0u && data == NULL) {
        hal_derr("hal_udp_write: data is NULL while len > 0");
        return 0u;
    }
    if (!s_udp.started || !s_udp.packet_started || len == 0u) {
        return 0u;
    }

    uint16_t room = (uint16_t)(MOCK_UDP_PAYLOAD_BUF_SIZE - s_udp.tx_len);
    uint16_t to_copy = len;
    if (to_copy > room) {
        to_copy = room;
    }

    if (to_copy > 0u) {
        memcpy(s_udp.tx_payload + s_udp.tx_len, data, to_copy);
        s_udp.tx_len = (uint16_t)(s_udp.tx_len + to_copy);
    }

    return to_copy;
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
    if (!s_udp.started || !s_udp.packet_started) {
        return false;
    }

    s_udp.packet_started = false;
    s_udp.end_packet_called = true;
    return s_udp.end_packet_result;
}

void hal_mock_udp_inject_packet(const char *remote_ip,
                                uint16_t remote_port,
                                const uint8_t *payload,
                                uint16_t len) {
    snprintf(s_udp.remote_ip,
             sizeof(s_udp.remote_ip),
             "%s",
             (remote_ip && remote_ip[0] != '\0') ? remote_ip : "0.0.0.0");
    s_udp.remote_port = remote_port;

    s_udp.rx_len = len;
    if (s_udp.rx_len > MOCK_UDP_PAYLOAD_BUF_SIZE) {
        s_udp.rx_len = MOCK_UDP_PAYLOAD_BUF_SIZE;
    }

    if (payload && s_udp.rx_len > 0u) {
        memcpy(s_udp.rx_payload, payload, s_udp.rx_len);
    }

    s_udp.rx_pos = 0u;
    s_udp.rx_pending = (s_udp.rx_len > 0u);
}

void hal_mock_udp_set_end_packet_result(bool result) {
    s_udp.end_packet_result = result;
}

uint16_t hal_mock_udp_get_local_port(void) {
    return s_udp.local_port;
}

const char *hal_mock_udp_get_last_begin_packet_host(void) {
    return s_udp.last_begin_packet_host;
}

uint16_t hal_mock_udp_get_last_begin_packet_port(void) {
    return s_udp.last_begin_packet_port;
}

const uint8_t *hal_mock_udp_get_last_tx_payload(void) {
    return s_udp.tx_payload;
}

uint16_t hal_mock_udp_get_last_tx_len(void) {
    return s_udp.tx_len;
}

bool hal_mock_udp_was_end_packet_called(void) {
    return s_udp.end_packet_called;
}

#endif /* HAL_ENABLE_UDP */

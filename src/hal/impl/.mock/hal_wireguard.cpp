#include "../../hal_config.h"

#ifdef HAL_ENABLE_WIREGUARD

#include "../../hal_wireguard.h"
#include "../../hal_serial.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

#define MOCK_WIREGUARD_TEXT_BUF 160u

static struct {
    bool initialized;
    bool begin_result;
    bool peer_up_result;
    bool kick_result;
    bool begin_advanced_called;

    uint8_t last_local_ip[HAL_WIREGUARD_IPV4_OCTETS];
    uint8_t last_allowed_ip[HAL_WIREGUARD_IPV4_OCTETS];
    uint8_t last_allowed_mask[HAL_WIREGUARD_IPV4_OCTETS];

    char last_private_key[MOCK_WIREGUARD_TEXT_BUF];
    char last_remote_peer_address[MOCK_WIREGUARD_TEXT_BUF];
    char last_remote_peer_public_key[MOCK_WIREGUARD_TEXT_BUF];
    uint16_t last_remote_peer_port;

    uint8_t peer_endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS];
    uint16_t peer_endpoint_port;

    uint8_t last_probe_ip[HAL_WIREGUARD_IPV4_OCTETS];
    uint16_t last_probe_port;
    uint32_t last_probe_min_interval_ms;
} s_wireguard;

static bool validate_non_empty(const char *value, const char *fn, const char *name) {
    if (!value || value[0] == '\0') {
        hal_derr("%s: %s is NULL/empty", fn, name);
        return false;
    }
    return true;
}

static bool validate_ip_ptr(const uint8_t *ip, const char *fn, const char *name) {
    if (!ip) {
        hal_derr("%s: %s is NULL", fn, name);
        return false;
    }
    return true;
}

void hal_mock_wireguard_reset(void) {
    memset(&s_wireguard, 0, sizeof(s_wireguard));
    s_wireguard.begin_result = true;
    s_wireguard.peer_up_result = false;
    s_wireguard.kick_result = true;
}

bool hal_wireguard_begin(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                         const char *private_key,
                         const char *remote_peer_address,
                         const char *remote_peer_public_key,
                         uint16_t remote_peer_port) {
    if (!validate_ip_ptr(local_ip, "hal_wireguard_begin", "local_ip")) {
        return false;
    }
    if (!validate_non_empty(private_key, "hal_wireguard_begin", "private_key")) {
        return false;
    }
    if (!validate_non_empty(remote_peer_address, "hal_wireguard_begin", "remote_peer_address")) {
        return false;
    }
    if (!validate_non_empty(remote_peer_public_key, "hal_wireguard_begin", "remote_peer_public_key")) {
        return false;
    }
    if (remote_peer_port == 0u) {
        hal_derr("hal_wireguard_begin: remote_peer_port must be > 0");
        return false;
    }

    memcpy(s_wireguard.last_local_ip, local_ip, HAL_WIREGUARD_IPV4_OCTETS);
    snprintf(s_wireguard.last_private_key, sizeof(s_wireguard.last_private_key), "%s", private_key);
    snprintf(s_wireguard.last_remote_peer_address,
             sizeof(s_wireguard.last_remote_peer_address),
             "%s",
             remote_peer_address);
    snprintf(s_wireguard.last_remote_peer_public_key,
             sizeof(s_wireguard.last_remote_peer_public_key),
             "%s",
             remote_peer_public_key);
    s_wireguard.last_remote_peer_port = remote_peer_port;
    s_wireguard.begin_advanced_called = false;

    s_wireguard.initialized = s_wireguard.begin_result;
    return s_wireguard.begin_result;
}

bool hal_wireguard_begin_advanced(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const char *private_key,
                                  const char *remote_peer_address,
                                  const char *remote_peer_public_key,
                                  uint16_t remote_peer_port,
                                  const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]) {
    if (!hal_wireguard_begin(local_ip,
                             private_key,
                             remote_peer_address,
                             remote_peer_public_key,
                             remote_peer_port)) {
        return false;
    }
    if (!validate_ip_ptr(allowed_ip, "hal_wireguard_begin_advanced", "allowed_ip")) {
        return false;
    }
    if (!validate_ip_ptr(allowed_mask, "hal_wireguard_begin_advanced", "allowed_mask")) {
        return false;
    }

    memcpy(s_wireguard.last_allowed_ip, allowed_ip, HAL_WIREGUARD_IPV4_OCTETS);
    memcpy(s_wireguard.last_allowed_mask, allowed_mask, HAL_WIREGUARD_IPV4_OCTETS);
    s_wireguard.begin_advanced_called = true;
    return true;
}

void hal_wireguard_end(void) {
    s_wireguard.initialized = false;
}

bool hal_wireguard_is_initialized(void) {
    return s_wireguard.initialized;
}

bool hal_wireguard_peer_up(char *endpoint_ip_out,
                           size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out) {
    if (endpoint_ip_out != NULL && endpoint_ip_out_size == 0u) {
        hal_derr("hal_wireguard_peer_up: endpoint_ip_out_size is 0");
        return false;
    }

    if (!s_wireguard.initialized || !s_wireguard.peer_up_result) {
        return false;
    }

    if (endpoint_ip_out) {
        if (snprintf(endpoint_ip_out,
                     endpoint_ip_out_size,
                     "%u.%u.%u.%u",
                     (unsigned)s_wireguard.peer_endpoint_ip[0],
                     (unsigned)s_wireguard.peer_endpoint_ip[1],
                     (unsigned)s_wireguard.peer_endpoint_ip[2],
                     (unsigned)s_wireguard.peer_endpoint_ip[3]) < 0) {
            hal_derr("hal_wireguard_peer_up: snprintf failed");
            return false;
        }
    }

    if (endpoint_port_out) {
        *endpoint_port_out = s_wireguard.peer_endpoint_port;
    }

    return true;
}

bool hal_wireguard_kick_handshake(const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  uint16_t probe_port,
                                  uint32_t min_interval_ms) {
    if (!validate_ip_ptr(probe_ip, "hal_wireguard_kick_handshake", "probe_ip")) {
        return false;
    }
    if (probe_port == 0u) {
        hal_derr("hal_wireguard_kick_handshake: probe_port must be > 0");
        return false;
    }
    if (!s_wireguard.initialized) {
        return false;
    }

    memcpy(s_wireguard.last_probe_ip, probe_ip, HAL_WIREGUARD_IPV4_OCTETS);
    s_wireguard.last_probe_port = probe_port;
    s_wireguard.last_probe_min_interval_ms = min_interval_ms;
    return s_wireguard.kick_result;
}

void hal_mock_wireguard_set_begin_result(bool result) {
    s_wireguard.begin_result = result;
}

void hal_mock_wireguard_set_peer_up_result(bool result) {
    s_wireguard.peer_up_result = result;
}

void hal_mock_wireguard_set_kick_result(bool result) {
    s_wireguard.kick_result = result;
}

void hal_mock_wireguard_set_initialized(bool initialized) {
    s_wireguard.initialized = initialized;
}

void hal_mock_wireguard_set_peer_endpoint(const uint8_t ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t port) {
    if (!ip) {
        return;
    }
    memcpy(s_wireguard.peer_endpoint_ip, ip, HAL_WIREGUARD_IPV4_OCTETS);
    s_wireguard.peer_endpoint_port = port;
}

const uint8_t *hal_mock_wireguard_get_last_local_ip(void) {
    return s_wireguard.last_local_ip;
}

const uint8_t *hal_mock_wireguard_get_last_allowed_ip(void) {
    return s_wireguard.last_allowed_ip;
}

const uint8_t *hal_mock_wireguard_get_last_allowed_mask(void) {
    return s_wireguard.last_allowed_mask;
}

const char *hal_mock_wireguard_get_last_remote_peer_address(void) {
    return s_wireguard.last_remote_peer_address;
}

uint16_t hal_mock_wireguard_get_last_remote_peer_port(void) {
    return s_wireguard.last_remote_peer_port;
}

bool hal_mock_wireguard_was_begin_advanced(void) {
    return s_wireguard.begin_advanced_called;
}

const uint8_t *hal_mock_wireguard_get_last_probe_ip(void) {
    return s_wireguard.last_probe_ip;
}

uint16_t hal_mock_wireguard_get_last_probe_port(void) {
    return s_wireguard.last_probe_port;
}

uint32_t hal_mock_wireguard_get_last_probe_min_interval_ms(void) {
    return s_wireguard.last_probe_min_interval_ms;
}

#endif /* HAL_ENABLE_WIREGUARD */

#pragma once

#include "jh_network_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAL_ENABLE_NETWORK_CORE) && !defined(HAL_NETWORK_BACKEND_CYW43) && \
    !defined(HAL_NETWORK_BACKEND_ESP_IDF)

const jh_network_wifi_ops_t *jh_public_network_wifi_ops(void);
const jh_network_resolver_ops_t *jh_public_network_resolver_ops(void);
const jh_network_tcp_ops_t *jh_public_network_tcp_ops(void);
const jh_network_udp_ops_t *jh_public_network_udp_ops(void);

#endif

#ifdef __cplusplus
}
#endif

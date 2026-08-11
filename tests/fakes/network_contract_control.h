#pragma once

#include "hal/network/hal_net.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void jh_contract_backend_reset(void);
bool jh_contract_backend_is_socket_offload(void);
void jh_contract_backend_tcp_inject(void *socket, const uint8_t *payload,
                                    size_t length);
void jh_contract_backend_udp_inject(void *socket,
                                    const hal_net_endpoint_t *remote,
                                    const uint8_t *payload, size_t length);

#ifdef __cplusplus
}
#endif

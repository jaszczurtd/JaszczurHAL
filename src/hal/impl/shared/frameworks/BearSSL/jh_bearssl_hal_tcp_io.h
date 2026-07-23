#pragma once

#include "hal/hal_config.h"

#ifdef HAL_ENABLE_TLS

#include "hal/hal_tcp.h"
#include "jh_bearssl_transport.h"

typedef struct {
  jh_bearssl_transport_t transport;
  hal_tcp_socket_t socket;
} jh_bearssl_hal_tcp_transport_t;

hal_status_t
jh_bearssl_hal_tcp_transport_init(jh_bearssl_hal_tcp_transport_t *adapter,
                                  hal_tcp_socket_t socket);

#endif

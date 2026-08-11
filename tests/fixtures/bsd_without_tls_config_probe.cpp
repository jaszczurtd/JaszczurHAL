#include "hal/core/hal_config.h"

#ifndef HAL_ENABLE_BSD_SOCKETS
#error "BSD probe must enable HAL_ENABLE_BSD_SOCKETS"
#endif
#ifndef HAL_ENABLE_TCP
#error "HAL_ENABLE_BSD_SOCKETS must propagate HAL_ENABLE_TCP"
#endif
#ifndef HAL_ENABLE_UDP
#error "HAL_ENABLE_BSD_SOCKETS must propagate HAL_ENABLE_UDP"
#endif
#ifdef HAL_ENABLE_TLS
#error "HAL_ENABLE_BSD_SOCKETS must remain usable without HAL_ENABLE_TLS"
#endif

int jh_bsd_without_tls_config_probe(void) { return 0; }

#include "hal/hal_config.h"

#ifndef HAL_ENABLE_TLS
#error "TLS probe must enable HAL_ENABLE_TLS"
#endif
#ifndef HAL_ENABLE_TCP
#error "HAL_ENABLE_TLS must propagate HAL_ENABLE_TCP"
#endif
#ifdef HAL_ENABLE_BSD_SOCKETS
#error "HAL_ENABLE_TLS must not force the optional BSD sockets adapter"
#endif

int jh_tls_without_bsd_config_probe(void) { return 0; }

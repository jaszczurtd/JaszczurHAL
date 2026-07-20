#pragma once

#include "../../hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_rp2040_tcp_reset_all(void);
hal_status_t jh_rp2040_udp_reset_all(void);

#ifdef __cplusplus
}
#endif

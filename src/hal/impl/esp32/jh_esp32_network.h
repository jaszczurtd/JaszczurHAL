#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_esp32_network_require_ready(void);
hal_status_t jh_esp32_network_stack_enter(bool require_ipv4);
void jh_esp32_network_stack_leave(void);
hal_status_t jh_esp32_network_underlay_netif(void **out_netif);

#ifdef __cplusplus
}
#endif

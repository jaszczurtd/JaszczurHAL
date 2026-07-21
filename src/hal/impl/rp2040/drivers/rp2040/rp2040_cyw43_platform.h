#pragma once

#include "../../../../hal_status.h"
#include "../../../../impl/shared/network/jh_network_service.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_rp2040_cyw43_platform_init(uint32_t country_code);
void jh_rp2040_cyw43_platform_deinit(void);
hal_status_t jh_rp2040_cyw43_platform_status(int status);
const jh_network_service_port_t *jh_rp2040_cyw43_platform_service_port(void);

#ifdef __cplusplus
}
#endif

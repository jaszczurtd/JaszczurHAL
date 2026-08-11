#pragma once

#include "hal/core/hal_status.h"
#include "hal/network/cyw43/jh_cyw43_radio_runtime.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_rp2040_cyw43_platform_init(uint32_t country_code);
hal_status_t jh_rp2040_cyw43_platform_deinit(void);
hal_status_t jh_rp2040_cyw43_platform_status(int status);
jh_cyw43_radio_runtime_t *jh_cyw43_radio_backend_runtime(void);

#ifdef __cplusplus
}
#endif

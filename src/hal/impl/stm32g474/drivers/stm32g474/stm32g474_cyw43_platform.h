#pragma once

#include "hal/network/cyw43/jh_cyw43_radio_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

jh_cyw43_radio_runtime_t *jh_cyw43_radio_backend_runtime(void);

#ifdef __cplusplus
}
#endif

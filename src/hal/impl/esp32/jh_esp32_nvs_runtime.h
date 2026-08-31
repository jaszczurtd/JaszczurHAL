#pragma once

#include "hal/core/hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the process-wide ESP-IDF NVS runtime once. */
hal_status_t jh_esp32_nvs_initialize(void);

#ifdef __cplusplus
}
#endif

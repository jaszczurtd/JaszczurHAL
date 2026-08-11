#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_rp_usb_flash_quiesce(uint32_t timeout_ms, bool *out_mutex_held);
hal_status_t jh_rp_usb_flash_resume(bool mutex_held);

#ifdef __cplusplus
}
#endif

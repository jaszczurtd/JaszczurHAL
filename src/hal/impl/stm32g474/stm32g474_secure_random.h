#pragma once

#include "hal/core/hal_status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_stm32g474_secure_random_bytes(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

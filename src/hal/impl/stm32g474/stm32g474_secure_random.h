#pragma once

#include "hal/hal_status.h"

#include <stddef.h>

hal_status_t jh_stm32g474_secure_random_bytes(void *buffer, size_t length);

#pragma once

#include "hal/hal_status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill a buffer with cryptographically usable random bytes.
 *
 * Targets provide the platform source: the Pico SDK generator on RP, the RNG
 * peripheral on STM32G474 and a deterministic generator on the host mock.
 * Backends without a source return HAL_EUNSUPPORTED and leave the buffer
 * zeroed, so callers stay fail-closed.
 */
hal_status_t jh_secure_random_bytes(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

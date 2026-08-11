#pragma once

#include "hal/core/hal_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bind a newly synchronized Unix time to the active target runtime clock. */
void jh_time_platform_apply_unix(uint64_t unix_time, uint32_t micros);

/** Read the shared clock with subsecond precision for target runtime hooks. */
hal_status_t jh_time_runtime_snapshot(uint64_t *out_unix, uint32_t *out_micros);

#ifdef __cplusplus
}
#endif

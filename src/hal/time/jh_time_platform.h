#pragma once

#include "hal/core/hal_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timeval;

/** Keep target libc bridges linked after the shared clock changes. */
void jh_time_platform_clock_changed(void);

/** Read the shared clock with subsecond precision for target runtime hooks. */
hal_status_t jh_time_runtime_snapshot(uint64_t *out_unix, uint32_t *out_micros);

/** Shared implementation behind target gettimeofday entry points. */
int jh_time_libc_gettimeofday(struct timeval *time_value);

/** Shared implementation behind target settimeofday entry points. */
int jh_time_libc_settimeofday(const struct timeval *time_value);

#ifdef __cplusplus
}
#endif

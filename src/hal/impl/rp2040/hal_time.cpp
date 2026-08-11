#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/time/jh_time_platform.h"

#include <sys/time.h>

void jh_time_platform_apply_unix(uint64_t unix_time, uint32_t micros) {
  struct timeval system_time = {};
  system_time.tv_sec = static_cast<time_t>(unix_time);
  system_time.tv_usec = static_cast<suseconds_t>(micros);
  (void)settimeofday(&system_time, nullptr);
}

#endif /* HAL_ENABLE_TIME */
#endif /* HAL_TARGET_IS_RP */

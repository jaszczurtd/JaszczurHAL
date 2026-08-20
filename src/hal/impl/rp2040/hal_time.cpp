#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/time/jh_time_platform.h"

#include <sys/time.h>

void jh_time_platform_clock_changed(void) {}

extern "C" int _gettimeofday(struct timeval *time_value, void *timezone) {
  (void)timezone;
  return jh_time_libc_gettimeofday(time_value);
}

extern "C" int settimeofday(const struct timeval *time_value,
                            const struct timezone *timezone) {
  (void)timezone;
  return jh_time_libc_settimeofday(time_value);
}

#endif /* HAL_ENABLE_TIME */
#endif /* HAL_TARGET_IS_RP */

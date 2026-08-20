#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/time/jh_time_platform.h"

#include <sys/time.h>

void jh_time_platform_clock_changed(void) {}

extern "C" int jh_stm32g474_runtime_gettimeofday(struct timeval *time_value) {
  return jh_time_libc_gettimeofday(time_value);
}

#if defined(JH_STM32G474_HW)
extern "C" int settimeofday(const struct timeval *time_value,
                            const struct timezone *timezone) {
  (void)timezone;
  return jh_time_libc_settimeofday(time_value);
}
#endif

#endif /* HAL_ENABLE_TIME */
#endif /* HAL_TARGET_IS_STM32G474 */

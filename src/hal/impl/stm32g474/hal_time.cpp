#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/time/jh_time_platform.h"

#include <errno.h>
#include <sys/time.h>

void jh_time_platform_apply_unix(uint64_t unix_time, uint32_t micros) {
  (void)unix_time;
  (void)micros;
}

extern "C" int jh_stm32g474_runtime_gettimeofday(struct timeval *time_value) {
  if (time_value == nullptr) {
    errno = EINVAL;
    return -1;
  }
  uint64_t unix_time = 0u;
  uint32_t micros = 0u;
  if (jh_time_runtime_snapshot(&unix_time, &micros) != HAL_OK) {
    time_value->tv_sec = 0;
    time_value->tv_usec = 0;
    errno = EAGAIN;
    return -1;
  }
  time_value->tv_sec = static_cast<time_t>(unix_time);
  time_value->tv_usec = static_cast<suseconds_t>(micros);
  return 0;
}

#endif /* HAL_ENABLE_TIME */
#endif /* HAL_TARGET_IS_STM32G474 */

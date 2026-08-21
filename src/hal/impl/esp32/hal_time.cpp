#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/time/jh_time_platform.h"

#include <sys/time.h>
#include <time.h>

void jh_time_platform_clock_changed(void) {
  uint64_t unix_seconds = 0u;
  uint32_t micros = 0u;
  if (jh_time_runtime_snapshot(&unix_seconds, &micros) != HAL_OK) {
    return;
  }

  const time_t seconds = static_cast<time_t>(unix_seconds);
  if (seconds < 0 || static_cast<uint64_t>(seconds) != unix_seconds) {
    return;
  }
  const struct timeval value = {seconds, static_cast<suseconds_t>(micros)};
  (void)settimeofday(&value, nullptr);
}

#endif /* HAL_ENABLE_TIME */
#endif /* HAL_TARGET_IS_ESP32_FAMILY */

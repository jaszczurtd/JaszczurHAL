#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/time/jh_time_platform.h"

void jh_time_platform_apply_unix(uint64_t unix_time, uint32_t micros) {
  (void)unix_time;
  (void)micros;
}

#endif /* HAL_ENABLE_TIME */
#endif /* HAL_TARGET_IS_MOCK */

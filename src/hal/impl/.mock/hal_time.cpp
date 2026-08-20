#include "hal/core/hal_target.h"

#if !HAL_TARGET_IS_MOCK
#error "Mock time translation unit selected for a hardware target"
#endif

#include "hal/time/jh_time_platform.h"

void jh_time_platform_clock_changed(void) {}

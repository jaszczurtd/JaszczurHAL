#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK || HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474

#include "hal/timers/hal_timer.h"

hal_alarm_id_t hal_timer_pool_add_alarm_us(hal_timer_pool_t pool,
                                           uint32_t delay_us,
                                           hal_alarm_callback_t callback,
                                           void *user_data, bool fire_if_past) {
  return hal_timer_pool_add_alarm_us_ex(pool, delay_us, callback, user_data,
                                        fire_if_past, nullptr);
}

#endif

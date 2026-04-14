#include "../../hal_timer.h"
#include <pico/time.h>
#include "hardware/timer.h"

// pico SDK: alarm_id_t = int32_t, callback = int64_t (*)(alarm_id_t, void*)
// HAL:      hal_alarm_id_t = int32_t, callback = int64_t (*)(hal_alarm_id_t, void*)
// ABI-compatible — cast is safe.

hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us, hal_alarm_callback_t callback,
                                      void *user_data, bool fire_if_past) {
    return add_alarm_in_us(delay_us, (alarm_callback_t)callback, user_data, fire_if_past);
}

bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id) {
    if (alarm_id == HAL_ALARM_INVALID) return false;
    return cancel_alarm(alarm_id);
}

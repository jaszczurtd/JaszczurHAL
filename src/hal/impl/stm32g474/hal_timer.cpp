#if !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32)

#include "../../hal_timer.h"
#include "../../hal_config.h"

#ifndef HAL_TIMER_MAX_ALARMS
#define HAL_TIMER_MAX_ALARMS 16
#endif

typedef struct {
    hal_alarm_id_t id;
    bool active;
} hal_timer_slot_t;

static hal_timer_slot_t s_slots[HAL_TIMER_MAX_ALARMS] = {};
static hal_alarm_id_t s_next_id = 1;

hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us,
                                      hal_alarm_callback_t callback,
                                      void *user_data,
                                      bool fire_if_past) {
    (void)delay_us;
    (void)callback;
    (void)user_data;
    (void)fire_if_past;

    for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
        if (!s_slots[i].active) {
            s_slots[i].active = true;
            s_slots[i].id = s_next_id++;
            return s_slots[i].id;
        }
    }

    HAL_ASSERT(0, "hal_timer: alarm pool exhausted - increase HAL_TIMER_MAX_ALARMS");
    return HAL_ALARM_INVALID;
}

bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id) {
    if (alarm_id == HAL_ALARM_INVALID) {
        return false;
    }

    for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
        if (s_slots[i].active && s_slots[i].id == alarm_id) {
            s_slots[i].active = false;
            return true;
        }
    }
    return false;
}

#endif /* !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32) */

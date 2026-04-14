#include "../../hal_timer.h"
#include "../../hal_config.h"
#include "hal_mock.h"
#include <string.h>

struct MockAlarm {
    hal_alarm_id_t       id;
    uint64_t             fire_at_us;
    hal_alarm_callback_t callback;
    void                *user_data;
    int                  active;
};

static uint64_t            s_current_us = 0;
static hal_alarm_id_t      s_next_id    = 1;
static MockAlarm           s_alarms[MOCK_MAX_ALARMS];
static int                 s_alarm_count = 0;

hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us, hal_alarm_callback_t callback,
                                      void *user_data, bool fire_if_past) {
    if (s_alarm_count >= hal_get_config()->mock_max_alarms) {
        HAL_ASSERT(0, "hal_timer: alarm pool exhausted - increase MOCK_MAX_ALARMS");
        return HAL_ALARM_INVALID;
    }
    (void)fire_if_past;
    MockAlarm *a   = &s_alarms[s_alarm_count++];
    a->id          = s_next_id++;
    a->fire_at_us  = s_current_us + delay_us;
    a->callback    = callback;
    a->user_data   = user_data;
    a->active      = 1;
    return a->id;
}

bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id) {
    for (int i = 0; i < s_alarm_count; i++) {
        if (s_alarms[i].id == alarm_id && s_alarms[i].active) {
            s_alarms[i].active = 0;
            return true;
        }
    }
    return false;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

void hal_mock_timer_advance_us(uint64_t us) {
    s_current_us += us;
    for (int i = 0; i < s_alarm_count; i++) {
        if (s_alarms[i].active && s_alarms[i].fire_at_us <= s_current_us) {
            s_alarms[i].active = 0;
            if (s_alarms[i].callback) {
                s_alarms[i].callback(s_alarms[i].id, s_alarms[i].user_data);
            }
        }
    }
}

uint64_t hal_mock_timer_get_us(void) {
    return s_current_us;
}

void hal_mock_timer_reset(void) {
    s_current_us = 0;
    s_next_id    = 1;
    s_alarm_count = 0;
    memset(s_alarms, 0, sizeof(s_alarms));
}

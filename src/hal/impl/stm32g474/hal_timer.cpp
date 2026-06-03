#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_timer.h"
#include "../../hal_config.h"

#ifndef HAL_TIMER_MAX_ALARMS
#define HAL_TIMER_MAX_ALARMS 16
#endif

#ifndef HAL_TIMER_MAX_POOLS
#define HAL_TIMER_MAX_POOLS 4
#endif

struct hal_timer_pool_impl_s {
    uint16_t max_timers;
};

typedef struct {
    hal_alarm_id_t   id;
    bool             active;
    hal_timer_pool_t owner_pool; // NULL -> default pool
} hal_timer_slot_t;

static hal_timer_slot_t s_slots[HAL_TIMER_MAX_ALARMS] = {};
static hal_alarm_id_t   s_next_id = 1;

static hal_timer_pool_impl_t s_pool_storage[HAL_TIMER_MAX_POOLS] = {};
static bool                  s_pool_used[HAL_TIMER_MAX_POOLS] = {};

static inline hal_timer_pool_t normalize_pool(hal_timer_pool_t pool) {
    return (pool == HAL_TIMER_POOL_DEFAULT) ? NULL : pool;
}

static inline void timer_store_result(hal_timer_result_t *out_result, hal_timer_result_t value) {
    if (out_result) {
        *out_result = value;
    }
}

static int pool_limit(hal_timer_pool_t pool) {
    pool = normalize_pool(pool);
    if (!pool) {
        return HAL_TIMER_MAX_ALARMS;
    }
    return (int)pool->max_timers;
}

static int pool_active_count(hal_timer_pool_t pool) {
    pool = normalize_pool(pool);
    int count = 0;
    for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
        if (s_slots[i].active && s_slots[i].owner_pool == pool) {
            count++;
        }
    }
    return count;
}

hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num, uint16_t max_timers) {
    (void)hardware_alarm_num;

    for (int i = 0; i < HAL_TIMER_MAX_POOLS; i++) {
        if (!s_pool_used[i]) {
            s_pool_used[i] = true;

            uint16_t capped = max_timers;
            if (capped == 0u) {
                capped = 1u;
            }
            if (capped > (uint16_t)HAL_TIMER_MAX_ALARMS) {
                capped = (uint16_t)HAL_TIMER_MAX_ALARMS;
            }

            s_pool_storage[i].max_timers = capped;
            return &s_pool_storage[i];
        }
    }

    HAL_ASSERT(0, "hal_timer_pool_create: pool slots exhausted");
    return NULL;
}

hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers) {
    return hal_timer_pool_create(0u, max_timers);
}

void hal_timer_pool_destroy(hal_timer_pool_t pool) {
    pool = normalize_pool(pool);
    if (!pool) {
        return;
    }

    for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
        if (s_slots[i].active && s_slots[i].owner_pool == pool) {
            s_slots[i].active = false;
            s_slots[i].owner_pool = NULL;
        }
    }

    for (int i = 0; i < HAL_TIMER_MAX_POOLS; i++) {
        if (&s_pool_storage[i] == pool) {
            s_pool_used[i] = false;
            s_pool_storage[i].max_timers = 0u;
            return;
        }
    }
}

hal_alarm_id_t hal_timer_pool_add_alarm_us(hal_timer_pool_t pool,
                                           uint32_t delay_us,
                                           hal_alarm_callback_t callback,
                                           void *user_data,
                                           bool fire_if_past) {
    return hal_timer_pool_add_alarm_us_ex(pool,
                                          delay_us,
                                          callback,
                                          user_data,
                                          fire_if_past,
                                          NULL);
}

hal_alarm_id_t hal_timer_pool_add_alarm_us_ex(hal_timer_pool_t pool,
                                              uint32_t delay_us,
                                              hal_alarm_callback_t callback,
                                              void *user_data,
                                              bool fire_if_past,
                                              hal_timer_result_t *out_result) {
    (void)user_data;

    if (!callback) {
        timer_store_result(out_result, HAL_TIMER_ERR_INVALID_ARG);
        return HAL_ALARM_INVALID;
    }
    if (delay_us == 0u && !fire_if_past) {
        timer_store_result(out_result, HAL_TIMER_ERR_TIME_PASSED);
        return HAL_ALARM_INVALID;
    }

    pool = normalize_pool(pool);

    if (pool_active_count(pool) >= pool_limit(pool)) {
        timer_store_result(out_result, HAL_TIMER_ERR_POOL_FULL);
        return HAL_ALARM_INVALID;
    }

    for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
        if (!s_slots[i].active) {
            s_slots[i].active = true;
            s_slots[i].owner_pool = pool;
            s_slots[i].id = s_next_id++;
            timer_store_result(out_result, HAL_TIMER_OK);
            return s_slots[i].id;
        }
    }

    timer_store_result(out_result, HAL_TIMER_ERR_NO_RESOURCE);
    return HAL_ALARM_INVALID;
}

bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool, hal_alarm_id_t alarm_id) {
    if (alarm_id == HAL_ALARM_INVALID) {
        return false;
    }

    pool = normalize_pool(pool);

    for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
        if (s_slots[i].active && s_slots[i].owner_pool == pool && s_slots[i].id == alarm_id) {
            s_slots[i].active = false;
            s_slots[i].owner_pool = NULL;
            return true;
        }
    }
    return false;
}

hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us,
                                      hal_alarm_callback_t callback,
                                      void *user_data,
                                      bool fire_if_past) {
    return hal_timer_add_alarm_us_ex(delay_us,
                                     callback,
                                     user_data,
                                     fire_if_past,
                                     NULL);
}

hal_alarm_id_t hal_timer_add_alarm_us_ex(uint32_t delay_us,
                                         hal_alarm_callback_t callback,
                                         void *user_data,
                                         bool fire_if_past,
                                         hal_timer_result_t *out_result) {
    return hal_timer_pool_add_alarm_us_ex(HAL_TIMER_POOL_DEFAULT,
                                          delay_us,
                                          callback,
                                          user_data,
                                          fire_if_past,
                                          out_result);
}

bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id) {
    return hal_timer_pool_cancel_alarm(HAL_TIMER_POOL_DEFAULT, alarm_id);
}

#endif  // HAL_TARGET_IS_STM32G474

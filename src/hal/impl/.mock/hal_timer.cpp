#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#include "hal/timers/hal_timer.h"
#include "hal_mock.h"
#include <new>
#include <string.h>

struct hal_timer_pool_impl_s {
  uint16_t max_timers;
};

struct MockAlarm {
  hal_alarm_id_t id;
  uint64_t fire_at_us;
  hal_alarm_callback_t callback;
  void *user_data;
  hal_timer_pool_t owner_pool; // NULL -> default pool
  int active;
};

static uint64_t s_current_us = 0;
static hal_alarm_id_t s_next_id = 1;
static MockAlarm s_alarms[MOCK_MAX_ALARMS];

static inline hal_timer_pool_t normalize_pool(hal_timer_pool_t pool) {
  return (pool == HAL_TIMER_POOL_DEFAULT) ? NULL : pool;
}

static inline void timer_store_result(hal_timer_result_t *out_result,
                                      hal_timer_result_t value) {
  if (out_result) {
    *out_result = value;
  }
}

static int pool_limit(hal_timer_pool_t pool) {
  hal_timer_pool_t p = normalize_pool(pool);
  if (!p) {
    int cfg_limit = hal_get_config()->mock_max_alarms;
    return (cfg_limit > 0) ? cfg_limit : MOCK_MAX_ALARMS;
  }
  return (int)p->max_timers;
}

static int pool_active_count(hal_timer_pool_t pool) {
  hal_timer_pool_t p = normalize_pool(pool);
  int count = 0;
  for (int i = 0; i < MOCK_MAX_ALARMS; i++) {
    if (s_alarms[i].active && s_alarms[i].owner_pool == p) {
      count++;
    }
  }
  return count;
}

static MockAlarm *find_free_slot(void) {
  for (int i = 0; i < MOCK_MAX_ALARMS; i++) {
    if (!s_alarms[i].active) {
      return &s_alarms[i];
    }
  }
  return NULL;
}

hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num,
                                       uint16_t max_timers) {
  (void)hardware_alarm_num;

  hal_timer_pool_t pool = new (std::nothrow) hal_timer_pool_impl_t();
  if (!pool) {
    HAL_ASSERT(0, "hal_timer_pool_create: allocation failed");
    return NULL;
  }

  uint16_t capped = max_timers;
  if (capped == 0u) {
    capped = 1u;
  }
  if (capped > (uint16_t)MOCK_MAX_ALARMS) {
    capped = (uint16_t)MOCK_MAX_ALARMS;
  }

  pool->max_timers = capped;
  return pool;
}

hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers) {
  return hal_timer_pool_create(0u, max_timers);
}

void hal_timer_pool_destroy(hal_timer_pool_t pool) {
  pool = normalize_pool(pool);
  if (!pool) {
    return;
  }

  for (int i = 0; i < MOCK_MAX_ALARMS; i++) {
    if (s_alarms[i].active && s_alarms[i].owner_pool == pool) {
      s_alarms[i].active = 0;
    }
  }

  delete pool;
}

hal_alarm_id_t hal_timer_pool_add_alarm_us_ex(
    hal_timer_pool_t pool, uint32_t delay_us, hal_alarm_callback_t callback,
    void *user_data, bool fire_if_past, hal_timer_result_t *out_result) {
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

  MockAlarm *a = find_free_slot();
  if (!a) {
    timer_store_result(out_result, HAL_TIMER_ERR_NO_RESOURCE);
    return HAL_ALARM_INVALID;
  }

  a->id = s_next_id++;
  a->fire_at_us = s_current_us + (uint64_t)delay_us;
  a->callback = callback;
  a->user_data = user_data;
  a->owner_pool = pool;
  a->active = 1;

  timer_store_result(out_result, HAL_TIMER_OK);
  return a->id;
}

bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool,
                                 hal_alarm_id_t alarm_id) {
  if (alarm_id == HAL_ALARM_INVALID) {
    return false;
  }

  pool = normalize_pool(pool);

  for (int i = 0; i < MOCK_MAX_ALARMS; i++) {
    if (s_alarms[i].active && s_alarms[i].owner_pool == pool &&
        s_alarms[i].id == alarm_id) {
      s_alarms[i].active = 0;
      return true;
    }
  }

  return false;
}

hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us,
                                      hal_alarm_callback_t callback,
                                      void *user_data, bool fire_if_past) {
  return hal_timer_add_alarm_us_ex(delay_us, callback, user_data, fire_if_past,
                                   NULL);
}

hal_alarm_id_t hal_timer_add_alarm_us_ex(uint32_t delay_us,
                                         hal_alarm_callback_t callback,
                                         void *user_data, bool fire_if_past,
                                         hal_timer_result_t *out_result) {
  return hal_timer_pool_add_alarm_us_ex(HAL_TIMER_POOL_DEFAULT, delay_us,
                                        callback, user_data, fire_if_past,
                                        out_result);
}

bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id) {
  return hal_timer_pool_cancel_alarm(HAL_TIMER_POOL_DEFAULT, alarm_id);
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

void hal_mock_timer_advance_us(uint64_t us) {
  s_current_us += us;

  for (int i = 0; i < MOCK_MAX_ALARMS; i++) {
    if (s_alarms[i].active && s_alarms[i].fire_at_us <= s_current_us) {
      int64_t next_delay = 0;
      if (s_alarms[i].callback) {
        next_delay =
            s_alarms[i].callback(s_alarms[i].id, s_alarms[i].user_data);
      }

      if (next_delay > 0) {
        s_alarms[i].fire_at_us = s_current_us + (uint64_t)next_delay;
      } else {
        s_alarms[i].active = 0;
      }
    }
  }
}

uint64_t hal_mock_timer_get_us(void) { return s_current_us; }

void hal_mock_timer_reset(void) {
  s_current_us = 0;
  s_next_id = 1;
  memset(s_alarms, 0, sizeof(s_alarms));
}
#endif // HAL_TARGET_IS_MOCK

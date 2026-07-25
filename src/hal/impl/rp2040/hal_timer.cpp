#include "../../hal_target.h"
#if HAL_TARGET_IS_RP
#include "../../hal_sync.h"
#include "../../hal_timer.h"
#include "../shared/hal_mutex_once.h"
#include "hardware/timer.h"
#include <new>
#include <pico/time.h>

// pico SDK: alarm_id_t = int32_t, callback = int64_t (*)(alarm_id_t, void*)
// HAL:      hal_alarm_id_t = int32_t, callback = int64_t (*)(hal_alarm_id_t,
// void*) ABI-compatible - cast is safe.

struct hal_timer_pool_impl_s {
  alarm_pool_t *pool;
};

static hal_mutex_t s_pool_api_mutex = NULL;

static void timer_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_pool_api_mutex);
}

static uint16_t timer_cap_entries(uint16_t max_timers) {
  // pico-time docs: pool size is effectively limited to 255 entries.
  if (max_timers == 0u)
    return 1u;
  if (max_timers > 255u)
    return 255u;
  return max_timers;
}

static inline alarm_pool_t *resolve_pool(hal_timer_pool_t pool) {
  if (pool && pool->pool)
    return pool->pool;
  return alarm_pool_get_default();
}

static inline void timer_store_result(hal_timer_result_t *out_result,
                                      hal_timer_result_t value) {
  if (out_result) {
    *out_result = value;
  }
}

hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num,
                                       uint16_t max_timers) {
  if (hardware_alarm_num >= NUM_ALARMS) {
    return NULL;
  }

  timer_ensure_mutex();
  hal_mutex_lock(s_pool_api_mutex);

  if (hardware_alarm_is_claimed((uint)hardware_alarm_num)) {
    hal_mutex_unlock(s_pool_api_mutex);
    return NULL;
  }

  hal_timer_pool_t h = new (std::nothrow) hal_timer_pool_impl_t();
  if (!h) {
    hal_mutex_unlock(s_pool_api_mutex);
    return NULL;
  }

  h->pool = alarm_pool_create((uint)hardware_alarm_num,
                              (uint)timer_cap_entries(max_timers));
  if (!h->pool) {
    delete h;
    h = NULL;
  }

  hal_mutex_unlock(s_pool_api_mutex);
  return h;
}

hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers) {
  timer_ensure_mutex();
  hal_mutex_lock(s_pool_api_mutex);

  // Probe whether at least one hardware alarm is free before allocating.
  // This avoids leaking a hal_timer_pool_impl_t if the pico-SDK helper
  // below would hard-assert. The probe + create sequence cannot fully
  // close the race against other SDK consumers, but the SDK helper itself
  // performs an atomic claim+create internally.
  int probe = hardware_alarm_claim_unused(false);
  if (probe < 0) {
    hal_mutex_unlock(s_pool_api_mutex);
    return NULL;
  }
  hardware_alarm_unclaim((uint)probe);

  hal_timer_pool_t h = new (std::nothrow) hal_timer_pool_impl_t();
  if (!h) {
    hal_mutex_unlock(s_pool_api_mutex);
    return NULL;
  }

  // alarm_pool_create_with_unused_hardware_alarm() claims an unused
  // hardware alarm and creates the pool atomically - no claim/unclaim
  // window for another consumer to steal the alarm.
  h->pool = alarm_pool_create_with_unused_hardware_alarm(
      (uint)timer_cap_entries(max_timers));
  if (!h->pool) {
    delete h;
    h = NULL;
  }

  hal_mutex_unlock(s_pool_api_mutex);
  return h;
}

void hal_timer_pool_destroy(hal_timer_pool_t pool) {
  if (!pool || pool == HAL_TIMER_POOL_DEFAULT)
    return;

  timer_ensure_mutex();
  hal_mutex_lock(s_pool_api_mutex);
  if (pool->pool) {
    alarm_pool_destroy(pool->pool);
    pool->pool = NULL;
  }
  delete pool;
  hal_mutex_unlock(s_pool_api_mutex);
}

hal_alarm_id_t hal_timer_pool_add_alarm_us(hal_timer_pool_t pool,
                                           uint32_t delay_us,
                                           hal_alarm_callback_t callback,
                                           void *user_data, bool fire_if_past) {
  return hal_timer_pool_add_alarm_us_ex(pool, delay_us, callback, user_data,
                                        fire_if_past, NULL);
}

hal_alarm_id_t hal_timer_pool_add_alarm_us_ex(
    hal_timer_pool_t pool, uint32_t delay_us, hal_alarm_callback_t callback,
    void *user_data, bool fire_if_past, hal_timer_result_t *out_result) {
  if (!callback) {
    timer_store_result(out_result, HAL_TIMER_ERR_INVALID_ARG);
    return HAL_ALARM_INVALID;
  }
  alarm_pool_t *target_pool = resolve_pool(pool);
  if (!target_pool) {
    timer_store_result(out_result, HAL_TIMER_ERR_NO_RESOURCE);
    return HAL_ALARM_INVALID;
  }

  alarm_id_t id = alarm_pool_add_alarm_in_us(target_pool, delay_us,
                                             (alarm_callback_t)callback,
                                             user_data, fire_if_past);
  if (id > 0) {
    timer_store_result(out_result, HAL_TIMER_OK);
    return (hal_alarm_id_t)id;
  }

  if (id == 0) {
    timer_store_result(out_result, HAL_TIMER_ERR_TIME_PASSED);
  } else {
    // pico-time reports negative value on pool exhaustion / scheduler failure.
    timer_store_result(out_result, HAL_TIMER_ERR_POOL_FULL);
  }
  return HAL_ALARM_INVALID;
}

bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool,
                                 hal_alarm_id_t alarm_id) {
  if (alarm_id == HAL_ALARM_INVALID)
    return false;
  alarm_pool_t *target_pool = resolve_pool(pool);
  if (!target_pool)
    return false;
  return alarm_pool_cancel_alarm(target_pool, (alarm_id_t)alarm_id);
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
#endif // HAL_TARGET_IS_RP

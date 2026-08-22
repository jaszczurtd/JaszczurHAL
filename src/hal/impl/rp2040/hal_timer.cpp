#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "hal/timers/hal_timer.h"
#include "hardware/timer.h"
#include <new>
#include <pico/time.h>

// pico SDK: alarm_id_t = int32_t, callback = int64_t (*)(alarm_id_t, void*)
// HAL:      hal_alarm_id_t = int32_t, callback = int64_t (*)(hal_alarm_id_t,
// void*) ABI-compatible - cast is safe.

struct rp_alarm_dispatch_entry_s {
  hal_alarm_callback_t callback;
  void *user_data;
  hal_alarm_id_t active_id;
  hal_alarm_id_t cancelled_id;
  uint32_t firing;
};

struct rp_alarm_dispatch_s {
  rp_alarm_dispatch_entry_s *entries;
  uint16_t entry_count;
  uint32_t publishing;
};

struct hal_timer_pool_impl_s {
  alarm_pool_t *pool;
  rp_alarm_dispatch_s dispatch;
};

static hal_mutex_t s_pool_api_mutex = NULL;
static rp_alarm_dispatch_entry_s
    s_default_dispatch_entries[PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS] = {};
static rp_alarm_dispatch_s s_default_dispatch = {
    s_default_dispatch_entries, PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS, 0u};

static bool timer_ensure_mutex(void) {
  return jh_hal_mutex_create_once(&s_pool_api_mutex) != NULL;
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

static inline rp_alarm_dispatch_s *resolve_dispatch(hal_timer_pool_t pool) {
  if (pool) {
    return &pool->dispatch;
  }
  return &s_default_dispatch;
}

static inline int16_t timer_alarm_index(hal_alarm_id_t id) {
  return (int16_t)(id >> 16);
}

static int64_t timer_dispatch_callback(alarm_id_t alarm_id, void *context) {
  rp_alarm_dispatch_s *dispatch = (rp_alarm_dispatch_s *)context;
  const int16_t index = timer_alarm_index((hal_alarm_id_t)alarm_id);
  if (!dispatch || index < 0 || (uint16_t)index >= dispatch->entry_count) {
    return 0;
  }

  rp_alarm_dispatch_entry_s &entry = dispatch->entries[index];
  __atomic_add_fetch(&entry.firing, 1u, __ATOMIC_ACQ_REL);

  int64_t delay_us = 0;
  const hal_alarm_id_t active_id =
      __atomic_load_n(&entry.active_id, __ATOMIC_ACQUIRE);
  hal_alarm_id_t published_id = active_id;
  uint32_t publishing = 0u;
  if (published_id != (hal_alarm_id_t)alarm_id) {
    publishing = __atomic_load_n(&dispatch->publishing, __ATOMIC_SEQ_CST);
    if (publishing == 0u) {
      /* Publication may have completed after the first active-ID load. */
      published_id = __atomic_load_n(&entry.active_id, __ATOMIC_ACQUIRE);
    }
  }
  if (published_id == (hal_alarm_id_t)alarm_id) {
    // callback/user_data are published before active_id with release ordering.
    // They remain unchanged until the Pico SDK has finished this callback and
    // can reuse the corresponding alarm-pool entry.
    hal_alarm_callback_t callback = entry.callback;
    void *user_data = entry.user_data;
    if (callback) {
      delay_us = callback((hal_alarm_id_t)alarm_id, user_data);
    }
    if (delay_us <= 0) {
      delay_us = 0;
      hal_alarm_id_t expected = (hal_alarm_id_t)alarm_id;
      (void)__atomic_compare_exchange_n(&entry.active_id, &expected,
                                        HAL_ALARM_INVALID, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
  } else if (publishing != 0u ||
             __atomic_load_n(&entry.cancelled_id, __ATOMIC_ACQUIRE) !=
                 (hal_alarm_id_t)alarm_id) {
    // The IRQ may run between the SDK allocating an entry and this backend
    // publishing the returned alarm ID. A dispatch-wide publication count
    // protects that otherwise unidentifiable entry; keep it alive and retry.
    // A cancelled ID stops only when no allocation is being published.
    delay_us = 1;
  }

  __atomic_sub_fetch(&entry.firing, 1u, __ATOMIC_ACQ_REL);
  return delay_us;
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

  if (!timer_ensure_mutex()) {
    return NULL;
  }
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

  const uint16_t entry_count = timer_cap_entries(max_timers);
  h->dispatch.entries =
      new (std::nothrow) rp_alarm_dispatch_entry_s[entry_count]();
  h->dispatch.entry_count = entry_count;
  if (!h->dispatch.entries) {
    delete h;
    hal_mutex_unlock(s_pool_api_mutex);
    return NULL;
  }

  h->pool = alarm_pool_create((uint)hardware_alarm_num, (uint)entry_count);
  if (!h->pool) {
    delete[] h->dispatch.entries;
    delete h;
    h = NULL;
  }

  hal_mutex_unlock(s_pool_api_mutex);
  return h;
}

hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers) {
  if (!timer_ensure_mutex()) {
    return NULL;
  }
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

  const uint16_t entry_count = timer_cap_entries(max_timers);
  h->dispatch.entries =
      new (std::nothrow) rp_alarm_dispatch_entry_s[entry_count]();
  h->dispatch.entry_count = entry_count;
  if (!h->dispatch.entries) {
    delete h;
    hal_mutex_unlock(s_pool_api_mutex);
    return NULL;
  }

  // alarm_pool_create_with_unused_hardware_alarm() claims an unused
  // hardware alarm and creates the pool atomically - no claim/unclaim
  // window for another consumer to steal the alarm.
  h->pool = alarm_pool_create_with_unused_hardware_alarm((uint)entry_count);
  if (!h->pool) {
    delete[] h->dispatch.entries;
    delete h;
    h = NULL;
  }

  hal_mutex_unlock(s_pool_api_mutex);
  return h;
}

void hal_timer_pool_destroy(hal_timer_pool_t pool) {
  if (!pool || pool == HAL_TIMER_POOL_DEFAULT)
    return;

  if (!timer_ensure_mutex()) {
    return;
  }
  hal_mutex_lock(s_pool_api_mutex);
  if (pool->pool) {
    alarm_pool_destroy(pool->pool);
    pool->pool = NULL;
  }
  delete[] pool->dispatch.entries;
  pool->dispatch.entries = NULL;
  pool->dispatch.entry_count = 0u;
  delete pool;
  hal_mutex_unlock(s_pool_api_mutex);
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

  rp_alarm_dispatch_s *dispatch = resolve_dispatch(pool);
  __atomic_add_fetch(&dispatch->publishing, 1u, __ATOMIC_SEQ_CST);
  alarm_id_t id = alarm_pool_add_alarm_in_us(
      target_pool, delay_us, timer_dispatch_callback, dispatch, fire_if_past);
  if (id > 0) {
    const int16_t index = timer_alarm_index((hal_alarm_id_t)id);
    if (!dispatch || index < 0 || (uint16_t)index >= dispatch->entry_count) {
      (void)alarm_pool_cancel_alarm(target_pool, id);
      __atomic_sub_fetch(&dispatch->publishing, 1u, __ATOMIC_SEQ_CST);
      timer_store_result(out_result, HAL_TIMER_ERR_INTERNAL);
      return HAL_ALARM_INVALID;
    }

    rp_alarm_dispatch_entry_s &entry = dispatch->entries[index];
    entry.callback = callback;
    entry.user_data = user_data;
    /* Pico alarm IDs repeat after 32767 allocations per slot. Retire a stale
     * cancellation marker before publishing a reused ID. publishing keeps a
     * pre-publication IRQ from mistaking the new alarm for that cancellation.
     */
    __atomic_store_n(&entry.cancelled_id, HAL_ALARM_INVALID, __ATOMIC_RELEASE);
    __atomic_store_n(&entry.active_id, (hal_alarm_id_t)id, __ATOMIC_RELEASE);
    __atomic_sub_fetch(&dispatch->publishing, 1u, __ATOMIC_SEQ_CST);
    timer_store_result(out_result, HAL_TIMER_OK);
    return (hal_alarm_id_t)id;
  }

  __atomic_sub_fetch(&dispatch->publishing, 1u, __ATOMIC_SEQ_CST);

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

  rp_alarm_dispatch_s *dispatch = resolve_dispatch(pool);
  const int16_t index = timer_alarm_index(alarm_id);
  rp_alarm_dispatch_entry_s *entry = NULL;
  if (dispatch && index >= 0 && (uint16_t)index < dispatch->entry_count) {
    entry = &dispatch->entries[index];
    __atomic_store_n(&entry->cancelled_id, alarm_id, __ATOMIC_RELEASE);
    hal_alarm_id_t expected = alarm_id;
    (void)__atomic_compare_exchange_n(&entry->active_id, &expected,
                                      HAL_ALARM_INVALID, false,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  }

  const bool cancelled =
      alarm_pool_cancel_alarm(target_pool, (alarm_id_t)alarm_id);

  // Pico's cancel operation only marks the SDK entry. On the other core the
  // IRQ may already have selected our stable dispatch record and be about to
  // enter it. Task-context cancellation drains that dispatch before returning;
  // exception context cannot wait (a callback cancelling itself would
  // deadlock), but managed-timer destruction is explicitly task-only.
  if (entry && __get_current_exception() == 0u) {
    while (__atomic_load_n(&entry->firing, __ATOMIC_ACQUIRE) != 0u) {
      tight_loop_contents();
    }
  }
  return cancelled;
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

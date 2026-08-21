#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal/timers/hal_timer.h"

#include <new>

struct hal_timer_impl_s {
  hal_timer_pool_t pool;
  uint32_t period_us;
  bool periodic;
  hal_timer_callback_t callback;
  void *user_data;

  hal_mutex_t mutex;

  hal_alarm_id_t alarm_id;
  hal_timer_state_t state;
  uint64_t next_fire_us;
  uint32_t paused_remaining_us;

  // Number of callback invocations currently in flight. Incremented at
  // callback entry and decremented at exit. Used by hal_timer_destroy() to
  // drain any pending callback before freeing the timer object.
  uint32_t in_callback;
};

static inline void timer_set_state_atomic(hal_timer_impl_t *t,
                                          hal_timer_state_t s) {
  __atomic_store_n(&t->state, s, __ATOMIC_RELEASE);
}

static inline hal_timer_state_t
timer_get_state_atomic(const hal_timer_impl_t *t) {
  return __atomic_load_n(&t->state, __ATOMIC_ACQUIRE);
}

static inline void timer_set_alarm_id_atomic(hal_timer_impl_t *t,
                                             hal_alarm_id_t id) {
  __atomic_store_n(&t->alarm_id, id, __ATOMIC_RELEASE);
}

static inline hal_alarm_id_t
timer_get_alarm_id_atomic(const hal_timer_impl_t *t) {
  return __atomic_load_n(&t->alarm_id, __ATOMIC_ACQUIRE);
}

static inline void timer_set_next_fire_atomic(hal_timer_impl_t *t,
                                              uint64_t next_fire_us) {
  __atomic_store_n(&t->next_fire_us, next_fire_us, __ATOMIC_RELEASE);
}

static inline uint64_t timer_get_next_fire_atomic(const hal_timer_impl_t *t) {
  return __atomic_load_n(&t->next_fire_us, __ATOMIC_ACQUIRE);
}

static inline void timer_set_period_atomic(hal_timer_impl_t *t,
                                           uint32_t period_us) {
  __atomic_store_n(&t->period_us, period_us, __ATOMIC_RELEASE);
}

static inline uint32_t timer_get_period_atomic(const hal_timer_impl_t *t) {
  return __atomic_load_n(&t->period_us, __ATOMIC_ACQUIRE);
}

static int64_t timer_internal_alarm_cb_body(hal_timer_impl_t *t) {
  if (timer_get_state_atomic(t) != HAL_TIMER_STATE_RUNNING) {
    timer_set_alarm_id_atomic(t, HAL_ALARM_INVALID);
    timer_set_next_fire_atomic(t, 0u);
    return 0;
  }

  if (t->callback) {
    t->callback(t, t->user_data);
  }

  if (!t->periodic) {
    if (timer_get_state_atomic(t) == HAL_TIMER_STATE_RUNNING) {
      timer_set_state_atomic(t, HAL_TIMER_STATE_STOPPED);
    }
    timer_set_alarm_id_atomic(t, HAL_ALARM_INVALID);
    timer_set_next_fire_atomic(t, 0u);
    return 0;
  }

  if (timer_get_state_atomic(t) != HAL_TIMER_STATE_RUNNING) {
    timer_set_alarm_id_atomic(t, HAL_ALARM_INVALID);
    timer_set_next_fire_atomic(t, 0u);
    return 0;
  }

  const uint32_t period = timer_get_period_atomic(t);
  if (period == 0u) {
    timer_set_state_atomic(t, HAL_TIMER_STATE_STOPPED);
    timer_set_alarm_id_atomic(t, HAL_ALARM_INVALID);
    timer_set_next_fire_atomic(t, 0u);
    return 0;
  }

  const uint64_t now = hal_micros64();
  timer_set_next_fire_atomic(t, now + (uint64_t)period);
  return (int64_t)period;
}

static int64_t timer_internal_alarm_cb(hal_alarm_id_t, void *user_data) {
  hal_timer_impl_t *t = (hal_timer_impl_t *)user_data;
  if (!t) {
    return 0;
  }

  __atomic_add_fetch(&t->in_callback, 1u, __ATOMIC_ACQ_REL);
  const int64_t rc = timer_internal_alarm_cb_body(t);
  __atomic_sub_fetch(&t->in_callback, 1u, __ATOMIC_ACQ_REL);
  return rc;
}

hal_timer_result_t hal_timer_create(hal_timer_pool_t pool, uint32_t period_us,
                                    bool periodic,
                                    hal_timer_callback_t callback,
                                    void *user_data, hal_timer_t *out_timer) {
  if (!callback || !out_timer || period_us == 0u) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  hal_timer_impl_t *t = new (std::nothrow) hal_timer_impl_t();
  if (!t) {
    return HAL_TIMER_ERR_NO_RESOURCE;
  }

  t->pool = pool;
  timer_set_period_atomic(t, period_us);
  t->periodic = periodic;
  t->callback = callback;
  t->user_data = user_data;

  t->mutex = hal_mutex_create();
  if (!t->mutex) {
    delete t;
    return HAL_TIMER_ERR_NO_RESOURCE;
  }

  timer_set_alarm_id_atomic(t, HAL_ALARM_INVALID);
  timer_set_state_atomic(t, HAL_TIMER_STATE_STOPPED);
  timer_set_next_fire_atomic(t, 0u);
  t->paused_remaining_us = 0u;
  __atomic_store_n(&t->in_callback, 0u, __ATOMIC_RELEASE);

  *out_timer = t;
  return HAL_TIMER_OK;
}

hal_timer_result_t hal_timer_destroy(hal_timer_t timer) {
  if (!timer) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  (void)hal_timer_stop(timer);

  // Drain any in-flight callback before freeing the object.
  // alarm_pool_cancel_alarm() does not synchronise against a callback that
  // is already executing on the alarm IRQ; without this wait the callback
  // could dereference freed memory.
  //
  // Caveat: this is a busy-wait. It must NOT be called from a context that
  // preempts the alarm IRQ (e.g. higher-priority ISR) - that would deadlock.
  while (__atomic_load_n(&timer->in_callback, __ATOMIC_ACQUIRE) != 0u) {
    // Spin; the in-flight callback will run to completion on its own core.
  }

  hal_mutex_t m = timer->mutex;
  timer->mutex = NULL;
  if (m) {
    hal_mutex_destroy(m);
  }
  delete timer;
  return HAL_TIMER_OK;
}

hal_timer_result_t hal_timer_start(hal_timer_t timer) {
  if (!timer) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  hal_mutex_lock(timer->mutex);
  if (timer_get_state_atomic(timer) == HAL_TIMER_STATE_RUNNING) {
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_ERR_ALREADY_RUNNING;
  }

  const uint32_t period = timer_get_period_atomic(timer);
  if (period == 0u || !timer->callback) {
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  timer_set_state_atomic(timer, HAL_TIMER_STATE_RUNNING);
  timer->paused_remaining_us = 0u;
  timer_set_next_fire_atomic(timer, hal_micros64() + (uint64_t)period);
  timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
  hal_mutex_unlock(timer->mutex);

  hal_timer_result_t add_res = HAL_TIMER_OK;
  hal_alarm_id_t id = hal_timer_pool_add_alarm_us_ex(
      timer->pool, period, timer_internal_alarm_cb, timer, false, &add_res);
  if (id == HAL_ALARM_INVALID) {
    hal_mutex_lock(timer->mutex);
    if (timer_get_state_atomic(timer) == HAL_TIMER_STATE_RUNNING) {
      timer_set_state_atomic(timer, HAL_TIMER_STATE_STOPPED);
      timer_set_next_fire_atomic(timer, 0u);
      timer->paused_remaining_us = 0u;
    }
    timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
    hal_mutex_unlock(timer->mutex);
    return add_res;
  }

  hal_mutex_lock(timer->mutex);
  if (timer_get_state_atomic(timer) == HAL_TIMER_STATE_RUNNING) {
    timer_set_alarm_id_atomic(timer, id);
  } else {
    hal_timer_pool_cancel_alarm(timer->pool, id);
  }
  hal_mutex_unlock(timer->mutex);
  return HAL_TIMER_OK;
}

hal_timer_result_t hal_timer_stop(hal_timer_t timer) {
  if (!timer) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  hal_mutex_lock(timer->mutex);
  if (timer_get_state_atomic(timer) == HAL_TIMER_STATE_STOPPED) {
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_ERR_NOT_RUNNING;
  }

  const hal_alarm_id_t id = timer_get_alarm_id_atomic(timer);
  timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
  timer_set_state_atomic(timer, HAL_TIMER_STATE_STOPPED);
  timer_set_next_fire_atomic(timer, 0u);
  timer->paused_remaining_us = 0u;
  hal_mutex_unlock(timer->mutex);

  if (id != HAL_ALARM_INVALID) {
    (void)hal_timer_pool_cancel_alarm(timer->pool, id);
  }

  return HAL_TIMER_OK;
}

hal_timer_result_t hal_timer_pause(hal_timer_t timer) {
  if (!timer) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  hal_mutex_lock(timer->mutex);
  if (timer_get_state_atomic(timer) != HAL_TIMER_STATE_RUNNING) {
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_ERR_NOT_RUNNING;
  }

  const uint64_t now = hal_micros64();
  const uint64_t next = timer_get_next_fire_atomic(timer);
  uint64_t rem64 = (next > now) ? (next - now) : 0u;
  if (rem64 == 0u) {
    rem64 = 1u;
  }
  if (rem64 > (uint64_t)UINT32_MAX) {
    rem64 = (uint64_t)UINT32_MAX;
  }

  const hal_alarm_id_t id = timer_get_alarm_id_atomic(timer);
  timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
  timer_set_state_atomic(timer, HAL_TIMER_STATE_PAUSED);
  timer->paused_remaining_us = (uint32_t)rem64;
  timer_set_next_fire_atomic(timer, 0u);
  hal_mutex_unlock(timer->mutex);

  if (id != HAL_ALARM_INVALID) {
    (void)hal_timer_pool_cancel_alarm(timer->pool, id);
  }

  return HAL_TIMER_OK;
}

hal_timer_result_t hal_timer_resume(hal_timer_t timer) {
  if (!timer) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  hal_mutex_lock(timer->mutex);
  if (timer_get_state_atomic(timer) != HAL_TIMER_STATE_PAUSED) {
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_ERR_NOT_PAUSED;
  }

  uint32_t delay = timer->paused_remaining_us;
  if (delay == 0u) {
    delay = timer_get_period_atomic(timer);
    if (delay == 0u) {
      hal_mutex_unlock(timer->mutex);
      return HAL_TIMER_ERR_INVALID_ARG;
    }
  }

  timer_set_state_atomic(timer, HAL_TIMER_STATE_RUNNING);
  timer->paused_remaining_us = 0u;
  timer_set_next_fire_atomic(timer, hal_micros64() + (uint64_t)delay);
  timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
  hal_mutex_unlock(timer->mutex);

  hal_timer_result_t add_res = HAL_TIMER_OK;
  hal_alarm_id_t id = hal_timer_pool_add_alarm_us_ex(
      timer->pool, delay, timer_internal_alarm_cb, timer, false, &add_res);
  if (id == HAL_ALARM_INVALID) {
    hal_mutex_lock(timer->mutex);
    if (timer_get_state_atomic(timer) == HAL_TIMER_STATE_RUNNING) {
      timer_set_state_atomic(timer, HAL_TIMER_STATE_PAUSED);
      timer->paused_remaining_us = delay;
      timer_set_next_fire_atomic(timer, 0u);
    }
    timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
    hal_mutex_unlock(timer->mutex);
    return add_res;
  }

  hal_mutex_lock(timer->mutex);
  if (timer_get_state_atomic(timer) == HAL_TIMER_STATE_RUNNING) {
    timer_set_alarm_id_atomic(timer, id);
  } else {
    hal_timer_pool_cancel_alarm(timer->pool, id);
  }
  hal_mutex_unlock(timer->mutex);
  return HAL_TIMER_OK;
}

hal_timer_result_t hal_timer_set_period_us(hal_timer_t timer,
                                           uint32_t period_us,
                                           bool restart_if_running) {
  if (!timer || period_us == 0u) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  hal_mutex_lock(timer->mutex);
  timer_set_period_atomic(timer, period_us);

  if (!restart_if_running ||
      timer_get_state_atomic(timer) != HAL_TIMER_STATE_RUNNING) {
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_OK;
  }

  const hal_alarm_id_t old_id = timer_get_alarm_id_atomic(timer);
  timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
  timer_set_next_fire_atomic(timer, hal_micros64() + (uint64_t)period_us);
  hal_mutex_unlock(timer->mutex);

  if (old_id != HAL_ALARM_INVALID) {
    (void)hal_timer_pool_cancel_alarm(timer->pool, old_id);
  }

  hal_timer_result_t add_res = HAL_TIMER_OK;
  hal_alarm_id_t new_id = hal_timer_pool_add_alarm_us_ex(
      timer->pool, period_us, timer_internal_alarm_cb, timer, false, &add_res);
  if (new_id == HAL_ALARM_INVALID) {
    hal_mutex_lock(timer->mutex);
    timer_set_state_atomic(timer, HAL_TIMER_STATE_STOPPED);
    timer_set_next_fire_atomic(timer, 0u);
    timer->paused_remaining_us = 0u;
    timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
    hal_mutex_unlock(timer->mutex);
    return add_res;
  }

  hal_mutex_lock(timer->mutex);
  if (timer_get_state_atomic(timer) == HAL_TIMER_STATE_RUNNING) {
    timer_set_alarm_id_atomic(timer, new_id);
  } else {
    hal_timer_pool_cancel_alarm(timer->pool, new_id);
  }
  hal_mutex_unlock(timer->mutex);
  return HAL_TIMER_OK;
}

hal_timer_result_t hal_timer_get_period_us(hal_timer_t timer,
                                           uint32_t *out_period_us) {
  if (!timer || !out_period_us) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  *out_period_us = timer_get_period_atomic(timer);
  return HAL_TIMER_OK;
}

hal_timer_state_t hal_timer_get_state(hal_timer_t timer) {
  if (!timer) {
    return HAL_TIMER_STATE_STOPPED;
  }
  return timer_get_state_atomic(timer);
}

hal_timer_result_t hal_timer_get_remaining_us(hal_timer_t timer,
                                              int64_t *out_remaining_us) {
  if (!timer || !out_remaining_us) {
    return HAL_TIMER_ERR_INVALID_ARG;
  }

  hal_mutex_lock(timer->mutex);
  const hal_timer_state_t state = timer_get_state_atomic(timer);

  if (state == HAL_TIMER_STATE_RUNNING) {
    const uint64_t now = hal_micros64();
    const uint64_t next = timer_get_next_fire_atomic(timer);
    const uint64_t rem = (next > now) ? (next - now) : 0u;
    *out_remaining_us = (int64_t)rem;
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_OK;
  }

  if (state == HAL_TIMER_STATE_PAUSED) {
    *out_remaining_us = (int64_t)timer->paused_remaining_us;
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_OK;
  }

  hal_mutex_unlock(timer->mutex);
  return HAL_TIMER_ERR_NOT_RUNNING;
}

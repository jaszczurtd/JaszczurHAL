#include "hal/core/hal_compiler.h"
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

  // Alarm that owns the timer, kAlarmArming while an arming call adds one,
  // HAL_ALARM_INVALID when disarmed. Only the owner runs the callback.
  hal_alarm_id_t alarm_id;
  hal_timer_state_t state;
  uint64_t next_fire_us;
  uint32_t paused_remaining_us;
  // Bumped under the mutex by every call that arms or disarms, so an arming
  // call publishes its alarm only when nothing replaced it meanwhile.
  uint32_t arm_seq;

  // Number of callback invocations currently in flight. Incremented at
  // callback entry and decremented at exit. Used by hal_timer_destroy() to
  // drain any pending callback before freeing the timer object.
  uint32_t in_callback;
};

// Backends never hand out 0 as an alarm id.
static constexpr hal_alarm_id_t kAlarmArming = 0;

static inline void timer_set_state_atomic(hal_timer_impl_t *t,
                                          hal_timer_state_t s) {
  HAL_ATOMIC_STORE(&t->state, s, HAL_ATOMIC_RELEASE);
}

static inline hal_timer_state_t
timer_get_state_atomic(const hal_timer_impl_t *t) {
  return HAL_ATOMIC_LOAD(&t->state, HAL_ATOMIC_ACQUIRE);
}

static inline void timer_set_alarm_id_atomic(hal_timer_impl_t *t,
                                             hal_alarm_id_t id) {
  HAL_ATOMIC_STORE(&t->alarm_id, id, HAL_ATOMIC_RELEASE);
}

static inline hal_alarm_id_t
timer_get_alarm_id_atomic(const hal_timer_impl_t *t) {
  return HAL_ATOMIC_LOAD(&t->alarm_id, HAL_ATOMIC_ACQUIRE);
}

static inline void timer_set_next_fire_atomic(hal_timer_impl_t *t,
                                              uint64_t next_fire_us) {
  HAL_ATOMIC_STORE(&t->next_fire_us, next_fire_us, HAL_ATOMIC_RELEASE);
}

static inline uint64_t timer_get_next_fire_atomic(const hal_timer_impl_t *t) {
  return HAL_ATOMIC_LOAD(&t->next_fire_us, HAL_ATOMIC_ACQUIRE);
}

static inline void timer_set_period_atomic(hal_timer_impl_t *t,
                                           uint32_t period_us) {
  HAL_ATOMIC_STORE(&t->period_us, period_us, HAL_ATOMIC_RELEASE);
}

static inline uint32_t timer_get_period_atomic(const hal_timer_impl_t *t) {
  return HAL_ATOMIC_LOAD(&t->period_us, HAL_ATOMIC_ACQUIRE);
}

static inline bool timer_alarm_is_real(hal_alarm_id_t id) {
  return id != HAL_ALARM_INVALID && id != kAlarmArming;
}

// true when alarm @p id owns the timer; adopts an alarm that fired before its
// arming call published the id.
static bool timer_alarm_owned(hal_timer_impl_t *t, hal_alarm_id_t id) {
  hal_alarm_id_t owner = kAlarmArming;
  return HAL_ATOMIC_COMPARE_EXCHANGE(&t->alarm_id, &owner, id,
                                     HAL_ATOMIC_ACQ_REL, HAL_ATOMIC_ACQUIRE) ||
         owner == id;
}

static int64_t timer_internal_alarm_cb_body(hal_timer_impl_t *t,
                                            hal_alarm_id_t id) {
  if (!timer_alarm_owned(t, id)) {
    return 0;
  }

  if (!t->periodic) {
    // Stop before the callback, so a start made meanwhile arms a new alarm.
    hal_timer_state_t running = HAL_TIMER_STATE_RUNNING;
    if (!HAL_ATOMIC_COMPARE_EXCHANGE(&t->state, &running,
                                     HAL_TIMER_STATE_STOPPED,
                                     HAL_ATOMIC_ACQ_REL, HAL_ATOMIC_ACQUIRE)) {
      return 0;
    }
    hal_alarm_id_t owner = id;
    (void)HAL_ATOMIC_COMPARE_EXCHANGE(&t->alarm_id, &owner, HAL_ALARM_INVALID,
                                      HAL_ATOMIC_ACQ_REL, HAL_ATOMIC_ACQUIRE);
    t->callback(t, t->user_data);
    return 0;
  }

  if (timer_get_state_atomic(t) != HAL_TIMER_STATE_RUNNING) {
    return 0;
  }
  t->callback(t, t->user_data);

  // A call made meanwhile re-armed or disarmed the timer and owns the rest.
  if (timer_get_alarm_id_atomic(t) != id ||
      timer_get_state_atomic(t) != HAL_TIMER_STATE_RUNNING) {
    return 0;
  }
  const uint32_t period = timer_get_period_atomic(t);
  timer_set_next_fire_atomic(t, hal_micros64() + (uint64_t)period);
  return (int64_t)period;
}

static int64_t timer_internal_alarm_cb(hal_alarm_id_t id, void *user_data) {
  hal_timer_impl_t *t = (hal_timer_impl_t *)user_data;
  if (!t) {
    return 0;
  }

  HAL_ATOMIC_ADD_FETCH(&t->in_callback, 1u, HAL_ATOMIC_ACQ_REL);
  const int64_t rc = timer_internal_alarm_cb_body(t, id);
  HAL_ATOMIC_SUB_FETCH(&t->in_callback, 1u, HAL_ATOMIC_ACQ_REL);
  return rc;
}

// Marks the timer RUNNING with an alarm on its way; caller holds the mutex.
static uint32_t timer_begin_arming_locked(hal_timer_impl_t *t,
                                          uint32_t delay_us) {
  timer_set_state_atomic(t, HAL_TIMER_STATE_RUNNING);
  t->paused_remaining_us = 0u;
  timer_set_next_fire_atomic(t, hal_micros64() + (uint64_t)delay_us);
  timer_set_alarm_id_atomic(t, kAlarmArming);
  return ++t->arm_seq;
}

// Revokes the owner alarm and returns it for cancelling; caller holds the
// mutex and cancels after unlocking.
static hal_alarm_id_t timer_disarm_locked(hal_timer_impl_t *t,
                                          hal_timer_state_t state,
                                          uint32_t remaining_us) {
  const hal_alarm_id_t id = timer_get_alarm_id_atomic(t);
  timer_set_alarm_id_atomic(t, HAL_ALARM_INVALID);
  timer_set_state_atomic(t, state);
  timer_set_next_fire_atomic(t, 0u);
  t->paused_remaining_us = remaining_us;
  t->arm_seq++;
  return id;
}

static void timer_cancel(hal_timer_impl_t *t, hal_alarm_id_t id) {
  if (timer_alarm_is_real(id)) {
    (void)hal_timer_pool_cancel_alarm(t->pool, id);
  }
}

// Adds the alarm for arming call @p seq. Publishes it unless a later call
// re-armed or disarmed the timer; then the alarm is cancelled instead. On
// failure the timer returns to @p fallback with @p delay_us kept for PAUSED.
static hal_timer_result_t timer_arm(hal_timer_impl_t *t, uint32_t seq,
                                    uint32_t delay_us,
                                    hal_timer_state_t fallback) {
  hal_timer_result_t result = HAL_TIMER_OK;
  const hal_alarm_id_t id = hal_timer_pool_add_alarm_us_ex(
      t->pool, delay_us, timer_internal_alarm_cb, t, false, &result);

  hal_mutex_lock(t->mutex);
  const bool current = t->arm_seq == seq;
  hal_alarm_id_t stale = HAL_ALARM_INVALID;
  if (id == HAL_ALARM_INVALID) {
    if (current) {
      (void)timer_disarm_locked(
          t, fallback, fallback == HAL_TIMER_STATE_PAUSED ? delay_us : 0u);
    }
  } else if (current && timer_get_state_atomic(t) == HAL_TIMER_STATE_RUNNING) {
    timer_set_alarm_id_atomic(t, id);
  } else {
    stale = id;
  }
  hal_mutex_unlock(t->mutex);

  timer_cancel(t, stale);
  return id == HAL_ALARM_INVALID ? result : HAL_TIMER_OK;
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
  t->arm_seq = 0u;
  HAL_ATOMIC_STORE(&t->in_callback, 0u, HAL_ATOMIC_RELEASE);

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
  while (HAL_ATOMIC_LOAD(&timer->in_callback, HAL_ATOMIC_ACQUIRE) != 0u) {
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

  const uint32_t seq = timer_begin_arming_locked(timer, period);
  hal_mutex_unlock(timer->mutex);
  return timer_arm(timer, seq, period, HAL_TIMER_STATE_STOPPED);
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
  const hal_alarm_id_t id =
      timer_disarm_locked(timer, HAL_TIMER_STATE_STOPPED, 0u);
  hal_mutex_unlock(timer->mutex);

  timer_cancel(timer, id);
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

  const hal_alarm_id_t id =
      timer_disarm_locked(timer, HAL_TIMER_STATE_PAUSED, (uint32_t)rem64);
  hal_mutex_unlock(timer->mutex);

  timer_cancel(timer, id);
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

  const uint32_t seq = timer_begin_arming_locked(timer, delay);
  hal_mutex_unlock(timer->mutex);
  return timer_arm(timer, seq, delay, HAL_TIMER_STATE_PAUSED);
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

  // Revoke the old alarm first: while it can still fire, no new alarm may be
  // waiting for adoption.
  const hal_alarm_id_t old_id = timer_get_alarm_id_atomic(timer);
  timer_set_alarm_id_atomic(timer, HAL_ALARM_INVALID);
  const uint32_t revoke_seq = ++timer->arm_seq;
  hal_mutex_unlock(timer->mutex);

  timer_cancel(timer, old_id);

  hal_mutex_lock(timer->mutex);
  if (timer->arm_seq != revoke_seq ||
      timer_get_state_atomic(timer) != HAL_TIMER_STATE_RUNNING) {
    hal_mutex_unlock(timer->mutex);
    return HAL_TIMER_OK;
  }
  const uint32_t seq = timer_begin_arming_locked(timer, period_us);
  hal_mutex_unlock(timer->mutex);
  return timer_arm(timer, seq, period_us, HAL_TIMER_STATE_STOPPED);
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

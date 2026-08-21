#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "hal/timers/hal_timer.h"

#include <driver/gptimer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef HAL_TIMER_MAX_ALARMS
#define HAL_TIMER_MAX_ALARMS 16
#endif

struct alarm_slot_t {
  hal_alarm_id_t id;
  bool active;
  bool firing;
  uint64_t deadline_us;
  hal_alarm_callback_t callback;
  void *user_data;
};

struct hal_timer_pool_impl_s {
  gptimer_handle_t timer;
  alarm_slot_t *slots;
  uint16_t capacity;
  hal_alarm_id_t next_id;
  portMUX_TYPE *lock;
  uint8_t selector;
  bool dynamically_allocated;
  bool destroying;
};

namespace {

constexpr uint8_t kHardwareTimerCount = 4u;

alarm_slot_t s_default_slots[HAL_TIMER_MAX_ALARMS] = {};
portMUX_TYPE s_default_lock = portMUX_INITIALIZER_UNLOCKED;
hal_timer_pool_impl_s s_default_pool = {nullptr,
                                        s_default_slots,
                                        HAL_TIMER_MAX_ALARMS,
                                        1,
                                        &s_default_lock,
                                        UINT8_MAX,
                                        false,
                                        false};
hal_mutex_t s_default_init_mutex;
hal_mutex_t s_selector_mutex;
bool s_selector_claimed[kHardwareTimerCount] = {};

void store_result(hal_timer_result_t *out_result, hal_timer_result_t value) {
  if (out_result != nullptr) {
    *out_result = value;
  }
}

hal_timer_pool_impl_s *resolve_pool(hal_timer_pool_t pool) {
  return pool == HAL_TIMER_POOL_DEFAULT ? &s_default_pool : pool;
}

void clear_slot(alarm_slot_t &slot) { slot = {}; }

int find_free_slot_locked(const hal_timer_pool_impl_s &pool) {
  for (uint16_t index = 0u; index < pool.capacity; ++index) {
    if (!pool.slots[index].active && !pool.slots[index].firing) {
      return (int)index;
    }
  }
  return -1;
}

int find_alarm_locked(const hal_timer_pool_impl_s &pool, hal_alarm_id_t id) {
  for (uint16_t index = 0u; index < pool.capacity; ++index) {
    if (pool.slots[index].active && pool.slots[index].id == id) {
      return (int)index;
    }
  }
  return -1;
}

bool alarm_id_in_use_locked(const hal_timer_pool_impl_s &pool,
                            hal_alarm_id_t id) {
  for (uint16_t index = 0u; index < pool.capacity; ++index) {
    const alarm_slot_t &slot = pool.slots[index];
    if ((slot.active || slot.firing) && slot.id == id) {
      return true;
    }
  }
  return false;
}

hal_alarm_id_t allocate_id_locked(hal_timer_pool_impl_s &pool) {
  for (;;) {
    const hal_alarm_id_t id = pool.next_id;
    pool.next_id = pool.next_id == INT32_MAX ? 1 : pool.next_id + 1;
    if (!alarm_id_in_use_locked(pool, id)) {
      return id;
    }
  }
}

bool read_counter(const hal_timer_pool_impl_s &pool, uint64_t *out_count) {
  return pool.timer != nullptr && out_count != nullptr &&
         gptimer_get_raw_count(pool.timer, out_count) == ESP_OK;
}

bool program_next_locked(hal_timer_pool_impl_s &pool, uint64_t now) {
  if (pool.timer == nullptr) {
    return false;
  }
  if (pool.destroying) {
    return gptimer_set_alarm_action(pool.timer, nullptr) == ESP_OK;
  }
  bool found = false;
  uint64_t deadline = 0u;
  for (uint16_t index = 0u; index < pool.capacity; ++index) {
    const alarm_slot_t &slot = pool.slots[index];
    if (!slot.active || slot.firing) {
      continue;
    }
    if (!found || slot.deadline_us < deadline) {
      deadline = slot.deadline_us;
      found = true;
    }
  }
  if (!found) {
    return gptimer_set_alarm_action(pool.timer, nullptr) == ESP_OK;
  }
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = deadline > now ? deadline : now + 1u;
  return gptimer_set_alarm_action(pool.timer, &alarm) == ESP_OK;
}

bool timer_alarm_callback(gptimer_handle_t,
                          const gptimer_alarm_event_data_t *event,
                          void *user_data) {
  hal_timer_pool_impl_s &pool =
      *static_cast<hal_timer_pool_impl_s *>(user_data);
  uint64_t now = event != nullptr ? event->count_value : 0u;
  for (;;) {
    int selected = -1;
    portENTER_CRITICAL_SAFE(pool.lock);
    if (pool.destroying) {
      portEXIT_CRITICAL_SAFE(pool.lock);
      return false;
    }
    for (uint16_t index = 0u; index < pool.capacity; ++index) {
      const alarm_slot_t &slot = pool.slots[index];
      if (!slot.active || slot.firing || slot.deadline_us > now) {
        continue;
      }
      if (selected < 0 || slot.deadline_us < pool.slots[selected].deadline_us) {
        selected = (int)index;
      }
    }
    if (selected < 0) {
      (void)program_next_locked(pool, now);
      portEXIT_CRITICAL_SAFE(pool.lock);
      return false;
    }

    alarm_slot_t &slot = pool.slots[selected];
    slot.firing = true;
    const hal_alarm_id_t id = slot.id;
    const hal_alarm_callback_t callback = slot.callback;
    void *const callback_data = slot.user_data;
    portEXIT_CRITICAL_SAFE(pool.lock);

    const int64_t next_delay =
        callback != nullptr ? callback(id, callback_data) : 0;
    uint64_t completed_at = now;
    (void)read_counter(pool, &completed_at);

    portENTER_CRITICAL_SAFE(pool.lock);
    if (pool.destroying) {
      if (slot.firing && slot.id == id) {
        clear_slot(slot);
      }
      portEXIT_CRITICAL_SAFE(pool.lock);
      return false;
    }
    if (slot.active && slot.id == id && slot.firing) {
      if (next_delay > 0) {
        slot.firing = false;
        slot.deadline_us = completed_at + (uint64_t)next_delay;
      } else {
        clear_slot(slot);
      }
    } else if (slot.firing && slot.id == id) {
      clear_slot(slot);
    }
    portEXIT_CRITICAL_SAFE(pool.lock);
    now = completed_at;
  }
}

bool initialize_pool_timer(hal_timer_pool_impl_s &pool) {
  gptimer_config_t config = {};
  config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  config.direction = GPTIMER_COUNT_UP;
  config.resolution_hz = UINT32_C(1000000);

  gptimer_handle_t timer = nullptr;
  esp_err_t result = gptimer_new_timer(&config, &timer);
  if (result == ESP_OK) {
    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = timer_alarm_callback;
    result = gptimer_register_event_callbacks(timer, &callbacks, &pool);
  }
  if (result == ESP_OK) {
    result = gptimer_enable(timer);
  }
  if (result == ESP_OK) {
    result = gptimer_set_raw_count(timer, 0u);
  }
  if (result == ESP_OK) {
    result = gptimer_start(timer);
  }
  if (result != ESP_OK) {
    if (timer != nullptr) {
      (void)gptimer_stop(timer);
      (void)gptimer_disable(timer);
      (void)gptimer_del_timer(timer);
    }
    return false;
  }
  // Default-pool first use is concurrent. Pair this publication with the
  // acquire load in initialize_default_timer() so no caller observes a
  // partially initialized GPTimer handle.
  __atomic_store_n(&pool.timer, timer, __ATOMIC_RELEASE);
  return true;
}

bool initialize_default_timer(void) {
  if (__atomic_load_n(&s_default_pool.timer, __ATOMIC_ACQUIRE) != nullptr) {
    return true;
  }
  if (xPortInIsrContext() != pdFALSE) {
    return false;
  }
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_default_init_mutex);
  if (mutex == nullptr) {
    return false;
  }
  hal_mutex_lock(mutex);
  bool initialized = s_default_pool.timer != nullptr;
  if (!initialized) {
    initialized = initialize_pool_timer(s_default_pool);
  }
  hal_mutex_unlock(mutex);
  return initialized;
}

hal_timer_pool_t create_pool(uint8_t selector, uint16_t max_timers) {
  if (selector >= kHardwareTimerCount || max_timers == 0u) {
    return nullptr;
  }
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_selector_mutex);
  if (mutex == nullptr) {
    return nullptr;
  }
  hal_mutex_lock(mutex);
  if (s_selector_claimed[selector]) {
    hal_mutex_unlock(mutex);
    return nullptr;
  }
  s_selector_claimed[selector] = true;
  hal_mutex_unlock(mutex);

  hal_timer_pool_impl_s *pool = static_cast<hal_timer_pool_impl_s *>(
      calloc(1u, sizeof(hal_timer_pool_impl_s)));
  alarm_slot_t *slots =
      static_cast<alarm_slot_t *>(calloc(max_timers, sizeof(alarm_slot_t)));
  portMUX_TYPE *lock =
      static_cast<portMUX_TYPE *>(malloc(sizeof(portMUX_TYPE)));
  if (pool == nullptr || slots == nullptr || lock == nullptr) {
    free(lock);
    free(slots);
    free(pool);
    hal_mutex_lock(mutex);
    s_selector_claimed[selector] = false;
    hal_mutex_unlock(mutex);
    return nullptr;
  }
  const portMUX_TYPE initializer = portMUX_INITIALIZER_UNLOCKED;
  *lock = initializer;
  pool->slots = slots;
  pool->capacity = max_timers;
  pool->next_id = 1;
  pool->lock = lock;
  pool->selector = selector;
  pool->dynamically_allocated = true;
  if (!initialize_pool_timer(*pool)) {
    free(lock);
    free(slots);
    free(pool);
    hal_mutex_lock(mutex);
    s_selector_claimed[selector] = false;
    hal_mutex_unlock(mutex);
    return nullptr;
  }
  return pool;
}

} // namespace

hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num,
                                       uint16_t max_timers) {
  return create_pool(hardware_alarm_num, max_timers);
}

hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers) {
  if (max_timers == 0u) {
    return nullptr;
  }
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_selector_mutex);
  if (mutex == nullptr) {
    return nullptr;
  }
  for (uint8_t selector = 0u; selector < kHardwareTimerCount; ++selector) {
    hal_mutex_lock(mutex);
    const bool available = !s_selector_claimed[selector];
    hal_mutex_unlock(mutex);
    if (available) {
      hal_timer_pool_t pool = create_pool(selector, max_timers);
      if (pool != nullptr) {
        return pool;
      }
    }
  }
  return nullptr;
}

void hal_timer_pool_destroy(hal_timer_pool_t pool_handle) {
  if (pool_handle == HAL_TIMER_POOL_DEFAULT) {
    return;
  }
  hal_timer_pool_impl_s *pool = resolve_pool(pool_handle);
  if (pool == nullptr || !pool->dynamically_allocated) {
    return;
  }
  if (xPortInIsrContext() != pdFALSE) {
    return;
  }

  portENTER_CRITICAL_SAFE(pool->lock);
  if (pool->destroying) {
    portEXIT_CRITICAL_SAFE(pool->lock);
    return;
  }
  pool->destroying = true;
  portEXIT_CRITICAL_SAFE(pool->lock);

  gptimer_handle_t timer = pool->timer;
  esp_err_t result = timer != nullptr ? gptimer_set_alarm_action(timer, nullptr)
                                      : ESP_ERR_INVALID_STATE;
  if (result == ESP_OK) {
    result = gptimer_stop(timer);
  }
  if (result == ESP_OK) {
    result = gptimer_disable(timer);
  }
  if (result == ESP_OK) {
    result = gptimer_del_timer(timer);
  }
  if (result != ESP_OK) {
    /* Keep the context and selector claimed. In particular, never free the
     * callback user_data unless ESP-IDF confirms that the interrupt handler
     * was removed by gptimer_del_timer(). The void public API cannot report a
     * teardown error, so a bounded leak is safer than a latent ISR UAF. */
    return;
  }
  pool->timer = nullptr;

  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_selector_mutex);
  if (mutex != nullptr) {
    hal_mutex_lock(mutex);
    if (pool->selector < kHardwareTimerCount) {
      s_selector_claimed[pool->selector] = false;
    }
    hal_mutex_unlock(mutex);
  }
  free(pool->lock);
  free(pool->slots);
  free(pool);
}

hal_alarm_id_t hal_timer_pool_add_alarm_us_ex(hal_timer_pool_t pool_handle,
                                              uint32_t delay_us,
                                              hal_alarm_callback_t callback,
                                              void *user_data,
                                              bool fire_if_past,
                                              hal_timer_result_t *out_result) {
  hal_timer_pool_impl_s *pool = resolve_pool(pool_handle);
  if (pool == nullptr || callback == nullptr) {
    store_result(out_result, HAL_TIMER_ERR_INVALID_ARG);
    return HAL_ALARM_INVALID;
  }
  if (delay_us == 0u && !fire_if_past) {
    store_result(out_result, HAL_TIMER_ERR_TIME_PASSED);
    return HAL_ALARM_INVALID;
  }
  if (pool == &s_default_pool && !initialize_default_timer()) {
    store_result(out_result, HAL_TIMER_ERR_NO_RESOURCE);
    return HAL_ALARM_INVALID;
  }
  if (pool->timer == nullptr) {
    store_result(out_result, HAL_TIMER_ERR_NO_RESOURCE);
    return HAL_ALARM_INVALID;
  }

  portENTER_CRITICAL_SAFE(pool->lock);
  if (pool->destroying) {
    portEXIT_CRITICAL_SAFE(pool->lock);
    store_result(out_result, HAL_TIMER_ERR_NO_RESOURCE);
    return HAL_ALARM_INVALID;
  }
  uint64_t now = 0u;
  if (!read_counter(*pool, &now)) {
    portEXIT_CRITICAL_SAFE(pool->lock);
    store_result(out_result, HAL_TIMER_ERR_INTERNAL);
    return HAL_ALARM_INVALID;
  }
  const int index = find_free_slot_locked(*pool);
  if (index < 0) {
    portEXIT_CRITICAL_SAFE(pool->lock);
    store_result(out_result, HAL_TIMER_ERR_POOL_FULL);
    return HAL_ALARM_INVALID;
  }
  alarm_slot_t &slot = pool->slots[index];
  slot.id = allocate_id_locked(*pool);
  slot.active = true;
  slot.firing = false;
  slot.deadline_us = now + (uint64_t)(delay_us == 0u ? 1u : delay_us);
  slot.callback = callback;
  slot.user_data = user_data;
  const hal_alarm_id_t id = slot.id;
  if (!program_next_locked(*pool, now)) {
    clear_slot(slot);
    portEXIT_CRITICAL_SAFE(pool->lock);
    store_result(out_result, HAL_TIMER_ERR_INTERNAL);
    return HAL_ALARM_INVALID;
  }
  portEXIT_CRITICAL_SAFE(pool->lock);
  store_result(out_result, HAL_TIMER_OK);
  return id;
}

bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool_handle,
                                 hal_alarm_id_t alarm_id) {
  hal_timer_pool_impl_s *pool = resolve_pool(pool_handle);
  if (pool == nullptr || alarm_id == HAL_ALARM_INVALID ||
      pool->timer == nullptr) {
    return false;
  }
  portENTER_CRITICAL_SAFE(pool->lock);
  if (pool->destroying) {
    portEXIT_CRITICAL_SAFE(pool->lock);
    return false;
  }
  uint64_t now = 0u;
  (void)read_counter(*pool, &now);
  const int index = find_alarm_locked(*pool, alarm_id);
  if (index < 0) {
    portEXIT_CRITICAL_SAFE(pool->lock);
    return false;
  }
  alarm_slot_t &slot = pool->slots[index];
  slot.active = false;
  const bool wait_for_callback = slot.firing && xPortInIsrContext() == pdFALSE;
  if (!slot.firing) {
    clear_slot(slot);
  }
  (void)program_next_locked(*pool, now);
  portEXIT_CRITICAL_SAFE(pool->lock);

  // Once the ISR has selected a slot it owns the copied callback/user_data
  // until that callback returns. A task-side cancel must therefore drain the
  // selected invocation before its caller can release user_data. ISR callers
  // cannot wait for themselves; they are handled by the callback epilogue.
  while (wait_for_callback) {
    portENTER_CRITICAL_SAFE(pool->lock);
    const bool completed = slot.id != alarm_id || !slot.firing;
    portEXIT_CRITICAL_SAFE(pool->lock);
    if (completed) {
      break;
    }
    taskYIELD();
  }
  return true;
}

hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us,
                                      hal_alarm_callback_t callback,
                                      void *user_data, bool fire_if_past) {
  return hal_timer_add_alarm_us_ex(delay_us, callback, user_data, fire_if_past,
                                   nullptr);
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

#endif // HAL_TARGET_IS_ESP32_FAMILY

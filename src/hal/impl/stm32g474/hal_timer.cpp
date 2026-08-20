#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#include "hal/system/hal_sync.h"
#include "hal/timers/hal_timer.h"

#include <stddef.h>
#include <stdint.h>

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"
#endif

#ifndef HAL_TIMER_MAX_ALARMS
#define HAL_TIMER_MAX_ALARMS 16
#endif

#ifndef HAL_TIMER_MAX_POOLS
#define HAL_TIMER_MAX_POOLS 4
#endif

#define STM32_TIMER_MAX_CHUNK_US 0xFFFFu

struct hal_timer_pool_impl_s {
  uint16_t max_timers;
};

typedef struct {
  hal_alarm_id_t id;
  bool active;
  bool firing;
  uint64_t deadline_us;
  hal_alarm_callback_t callback;
  void *user_data;
  hal_timer_pool_t owner_pool; // NULL -> default pool
} stm32_alarm_slot_t;

static stm32_alarm_slot_t s_slots[HAL_TIMER_MAX_ALARMS] = {};
static hal_alarm_id_t s_next_id = 1;
static uint64_t s_now_us = 0u;
static uint32_t s_programmed_us = 0u;

static hal_timer_pool_impl_t s_pool_storage[HAL_TIMER_MAX_POOLS] = {};
static bool s_pool_used[HAL_TIMER_MAX_POOLS] = {};

#ifdef JH_STM32G474_HW
static bool s_hw_ready = false;
#endif

static inline hal_timer_pool_t normalize_pool(hal_timer_pool_t pool) {
  return (pool == HAL_TIMER_POOL_DEFAULT) ? NULL : pool;
}

static inline void timer_store_result(hal_timer_result_t *out_result,
                                      hal_timer_result_t value) {
  if (out_result) {
    *out_result = value;
  }
}

static inline uint32_t clamp_delay_us(uint64_t delay_us) {
  if (delay_us == 0u) {
    return 1u;
  }
  if (delay_us > STM32_TIMER_MAX_CHUNK_US) {
    return STM32_TIMER_MAX_CHUNK_US;
  }
  return (uint32_t)delay_us;
}

static bool pool_is_live(hal_timer_pool_t pool) {
  pool = normalize_pool(pool);
  if (!pool) {
    return true;
  }

  for (int i = 0; i < HAL_TIMER_MAX_POOLS; i++) {
    if (&s_pool_storage[i] == pool) {
      return s_pool_used[i];
    }
  }
  return false;
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

static int find_free_slot(void) {
  for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
    if (!s_slots[i].active) {
      return i;
    }
  }
  return -1;
}

static hal_alarm_id_t next_alarm_id(void) {
  hal_alarm_id_t id = s_next_id++;
  if (s_next_id <= 0) {
    s_next_id = 1;
  }
  if (id == HAL_ALARM_INVALID || id <= 0) {
    id = s_next_id++;
  }
  return id;
}

static int find_due_slot(void) {
  int best = -1;
  uint64_t best_deadline = 0u;

  for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
    if (!s_slots[i].active || s_slots[i].firing ||
        s_slots[i].deadline_us > s_now_us) {
      continue;
    }

    if (best < 0 || s_slots[i].deadline_us < best_deadline) {
      best = i;
      best_deadline = s_slots[i].deadline_us;
    }
  }

  return best;
}

static bool find_next_deadline(uint64_t *out_deadline) {
  bool found = false;
  uint64_t best = 0u;

  for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
    if (!s_slots[i].active || s_slots[i].firing) {
      continue;
    }
    if (!found || s_slots[i].deadline_us < best) {
      found = true;
      best = s_slots[i].deadline_us;
    }
  }

  if (found && out_deadline) {
    *out_deadline = best;
  }
  return found;
}

#ifdef JH_STM32G474_HW
static void timer_hw_disable(void) {
  if (!s_hw_ready) {
    return;
  }

  TIM_CR1(TIM6_BASE) &= ~TIM_CR1_CEN;
  TIM_DIER(TIM6_BASE) &= ~TIM_DIER_UIE;
  TIM_SR(TIM6_BASE) = 0u;
}

static void timer_hw_init(void) {
  if (s_hw_ready) {
    return;
  }

  RCC_APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
  (void)RCC_APB1ENR1;

  TIM_CR1(TIM6_BASE) = 0u;
  TIM_DIER(TIM6_BASE) = 0u;
  TIM_PSC(TIM6_BASE) = (JH_G474_TIMCLK1_HZ / 1000000u) - 1u;
  TIM_ARR(TIM6_BASE) = STM32_TIMER_MAX_CHUNK_US - 1u;
  TIM_CNT(TIM6_BASE) = 0u;
  TIM_EGR(TIM6_BASE) = TIM_EGR_UG;
  TIM_SR(TIM6_BASE) = 0u;

  NVIC_IPR8(TIM6_DACUNDER_IRQn) = JH_NVIC_PRIO_TIMER;
  NVIC_ICPR(TIM6_DACUNDER_IRQn >> 5u) = (1u << (TIM6_DACUNDER_IRQn & 31u));
  NVIC_ISER(TIM6_DACUNDER_IRQn >> 5u) = (1u << (TIM6_DACUNDER_IRQn & 31u));

  s_hw_ready = true;
}

static void timer_hw_program(uint32_t delay_us) {
  timer_hw_init();

  TIM_CR1(TIM6_BASE) &= ~TIM_CR1_CEN;
  TIM_DIER(TIM6_BASE) = TIM_DIER_UIE;
  TIM_CNT(TIM6_BASE) = 0u;
  TIM_ARR(TIM6_BASE) = delay_us - 1u;
  TIM_SR(TIM6_BASE) = 0u;
  TIM_CR1(TIM6_BASE) = TIM_CR1_OPM | TIM_CR1_CEN;
}

static void timer_refresh_now_locked(void) {
  if (!s_hw_ready || s_programmed_us == 0u) {
    return;
  }

  uint32_t elapsed = 0u;
  const uint32_t sr = TIM_SR(TIM6_BASE);

  if ((sr & TIM_SR_UIF) != 0u) {
    TIM_CR1(TIM6_BASE) &= ~TIM_CR1_CEN;
    TIM_SR(TIM6_BASE) = 0u;
    elapsed = s_programmed_us;
    s_programmed_us = 0u;
  } else if ((TIM_CR1(TIM6_BASE) & TIM_CR1_CEN) != 0u) {
    uint32_t cnt = TIM_CNT(TIM6_BASE);
    if (cnt > s_programmed_us) {
      cnt = s_programmed_us;
    }
    if (cnt == 0u) {
      return;
    }

    TIM_CR1(TIM6_BASE) &= ~TIM_CR1_CEN;
    elapsed = cnt;
    s_programmed_us -= cnt;
  }

  s_now_us += (uint64_t)elapsed;
}
#else
static void timer_refresh_now_locked(void) {}
#endif

static void timer_program_next_locked(void) {
  uint64_t next = 0u;
  if (!find_next_deadline(&next)) {
    s_programmed_us = 0u;
#ifdef JH_STM32G474_HW
    timer_hw_disable();
#endif
    return;
  }

  const uint64_t delta = (next > s_now_us) ? (next - s_now_us) : 1u;
  s_programmed_us = clamp_delay_us(delta);

#ifdef JH_STM32G474_HW
  timer_hw_program(s_programmed_us);
#endif
}

static void timer_lock(void) { hal_critical_section_enter(); }
static void timer_unlock(void) { hal_critical_section_exit(); }

static void timer_dispatch_due_locked(void) {
  for (;;) {
    const int idx = find_due_slot();
    if (idx < 0) {
      timer_program_next_locked();
      return;
    }

    s_slots[idx].firing = true;
    const hal_alarm_id_t id = s_slots[idx].id;
    hal_alarm_callback_t cb = s_slots[idx].callback;
    void *user_data = s_slots[idx].user_data;

    timer_unlock();
    const int64_t next_delay = cb ? cb(id, user_data) : 0;
    timer_lock();

    if (s_slots[idx].active && s_slots[idx].id == id && s_slots[idx].firing) {
      s_slots[idx].firing = false;
      if (next_delay > 0) {
        s_slots[idx].deadline_us = s_now_us + (uint64_t)next_delay;
      } else {
        s_slots[idx].active = false;
        s_slots[idx].callback = nullptr;
        s_slots[idx].user_data = nullptr;
        s_slots[idx].owner_pool = NULL;
      }
    }
  }
}

hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num,
                                       uint16_t max_timers) {
  if (hardware_alarm_num >= HAL_TIMER_MAX_POOLS) {
    return NULL;
  }

  timer_lock();
  if (s_pool_used[hardware_alarm_num]) {
    timer_unlock();
    return NULL;
  }

  uint16_t capped = max_timers;
  if (capped == 0u) {
    capped = 1u;
  }
  if (capped > (uint16_t)HAL_TIMER_MAX_ALARMS) {
    capped = (uint16_t)HAL_TIMER_MAX_ALARMS;
  }

  s_pool_used[hardware_alarm_num] = true;
  s_pool_storage[hardware_alarm_num].max_timers = capped;
  timer_unlock();

  return &s_pool_storage[hardware_alarm_num];
}

hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers) {
  timer_lock();
  for (uint8_t i = 0; i < HAL_TIMER_MAX_POOLS; i++) {
    if (!s_pool_used[i]) {
      timer_unlock();
      return hal_timer_pool_create(i, max_timers);
    }
  }
  timer_unlock();

  return NULL;
}

void hal_timer_pool_destroy(hal_timer_pool_t pool) {
  pool = normalize_pool(pool);
  if (!pool) {
    return;
  }

  timer_lock();
  timer_refresh_now_locked();
  if (!pool_is_live(pool)) {
    timer_program_next_locked();
    timer_unlock();
    return;
  }

  for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
    if (s_slots[i].active && s_slots[i].owner_pool == pool) {
      s_slots[i].active = false;
      s_slots[i].firing = false;
      s_slots[i].callback = nullptr;
      s_slots[i].user_data = nullptr;
      s_slots[i].owner_pool = NULL;
    }
  }

  for (int i = 0; i < HAL_TIMER_MAX_POOLS; i++) {
    if (&s_pool_storage[i] == pool) {
      s_pool_used[i] = false;
      s_pool_storage[i].max_timers = 0u;
      break;
    }
  }

  timer_program_next_locked();
  timer_unlock();
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

  timer_lock();
  timer_refresh_now_locked();
  if (!pool_is_live(pool)) {
    timer_program_next_locked();
    timer_unlock();
    timer_store_result(out_result, HAL_TIMER_ERR_INVALID_ARG);
    return HAL_ALARM_INVALID;
  }
  if (pool_active_count(pool) >= pool_limit(pool)) {
    timer_program_next_locked();
    timer_unlock();
    timer_store_result(out_result, HAL_TIMER_ERR_POOL_FULL);
    return HAL_ALARM_INVALID;
  }

  const int slot_idx = find_free_slot();
  if (slot_idx < 0) {
    timer_program_next_locked();
    timer_unlock();
    timer_store_result(out_result, HAL_TIMER_ERR_NO_RESOURCE);
    return HAL_ALARM_INVALID;
  }

  const uint32_t armed_delay = (delay_us == 0u) ? 1u : delay_us;
  stm32_alarm_slot_t *slot = &s_slots[slot_idx];
  slot->id = next_alarm_id();
  slot->active = true;
  slot->firing = false;
  slot->deadline_us = s_now_us + (uint64_t)armed_delay;
  slot->callback = callback;
  slot->user_data = user_data;
  slot->owner_pool = pool;

  const hal_alarm_id_t id = slot->id;
  timer_program_next_locked();
  timer_unlock();

  timer_store_result(out_result, HAL_TIMER_OK);
  return id;
}

bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool,
                                 hal_alarm_id_t alarm_id) {
  if (alarm_id == HAL_ALARM_INVALID) {
    return false;
  }

  pool = normalize_pool(pool);

  timer_lock();
  timer_refresh_now_locked();
  for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
    if (s_slots[i].active && s_slots[i].owner_pool == pool &&
        s_slots[i].id == alarm_id) {
      s_slots[i].active = false;
      s_slots[i].firing = false;
      s_slots[i].callback = nullptr;
      s_slots[i].user_data = nullptr;
      s_slots[i].owner_pool = NULL;
      timer_program_next_locked();
      timer_unlock();
      return true;
    }
  }
  timer_program_next_locked();
  timer_unlock();

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

#ifdef JH_STM32G474_HW
extern "C" void TIM6_DACUNDER_IRQHandler(void) {
  if ((TIM_SR(TIM6_BASE) & TIM_SR_UIF) == 0u) {
    return;
  }

  TIM_CR1(TIM6_BASE) &= ~TIM_CR1_CEN;
  TIM_SR(TIM6_BASE) = 0u;

  timer_lock();
  s_now_us += (uint64_t)s_programmed_us;
  s_programmed_us = 0u;
  timer_dispatch_due_locked();
  timer_unlock();
}
#else
extern "C" void hal_stm32g474_timer_test_reset(void) {
  timer_lock();
  for (int i = 0; i < HAL_TIMER_MAX_ALARMS; i++) {
    s_slots[i] = {};
  }
  for (int i = 0; i < HAL_TIMER_MAX_POOLS; i++) {
    s_pool_storage[i] = {};
    s_pool_used[i] = false;
  }
  s_next_id = 1;
  s_now_us = 0u;
  s_programmed_us = 0u;
  timer_unlock();
}

extern "C" void hal_stm32g474_timer_test_advance_us(uint64_t us) {
  timer_lock();
  s_now_us += us;
  timer_dispatch_due_locked();
  timer_unlock();
}

extern "C" uint64_t hal_stm32g474_timer_test_now_us(void) { return s_now_us; }
#endif

#endif // HAL_TARGET_IS_STM32G474

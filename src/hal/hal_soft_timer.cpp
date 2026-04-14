#include "hal_soft_timer.h"
#include "hal_serial.h"

#include <new>

#include <utils/SmartTimers.h>

struct hal_soft_timer_impl_s {
  SmartTimers timer;
};

static SmartTimers *hal_soft_timer_get(hal_soft_timer_t timer) {
  return (timer != nullptr) ? &timer->timer : nullptr;
}

hal_soft_timer_t hal_soft_timer_create(void) {
  return new (std::nothrow) hal_soft_timer_impl_s();
}

void hal_soft_timer_destroy(hal_soft_timer_t timer) {
  delete timer;
}

bool hal_soft_timer_begin(hal_soft_timer_t timer,
                          hal_soft_timer_callback_t callback,
                          uint32_t interval_ms) {
  SmartTimers *t = hal_soft_timer_get(timer);
  if (t == nullptr) {
    return false;
  }

  t->begin(callback, interval_ms);
  return true;
}

void hal_soft_timer_restart(hal_soft_timer_t timer) {
  SmartTimers *t = hal_soft_timer_get(timer);
  if (t != nullptr) {
    t->restart();
  }
}

bool hal_soft_timer_available(hal_soft_timer_t timer) {
  SmartTimers *t = hal_soft_timer_get(timer);
  return (t != nullptr) ? t->available() : false;
}

uint32_t hal_soft_timer_time_left(hal_soft_timer_t timer) {
  SmartTimers *t = hal_soft_timer_get(timer);
  return (t != nullptr) ? t->time() : 0u;
}

void hal_soft_timer_set_interval(hal_soft_timer_t timer, uint32_t interval_ms) {
  SmartTimers *t = hal_soft_timer_get(timer);
  if (t != nullptr) {
    t->time(interval_ms);
  }
}

void hal_soft_timer_tick(hal_soft_timer_t timer) {
  SmartTimers *t = hal_soft_timer_get(timer);
  if (t != nullptr) {
    t->tick();
  }
}

void hal_soft_timer_abort(hal_soft_timer_t timer) {
  SmartTimers *t = hal_soft_timer_get(timer);
  if (t != nullptr) {
    t->abort();
  }
}

bool hal_soft_timer_setup_table(const hal_soft_timer_table_entry_t *table,
                                uint32_t count,
                                void (*idle_cb)(void),
                                uint32_t delay_ms) {
  if (table == nullptr) {
    hal_derr("hal_soft_timer_setup_table: table is NULL");
    return false;
  }
  if (count == 0u) {
    hal_derr("hal_soft_timer_setup_table: count is 0");
    return false;
  }
  for (uint32_t i = 0u; i < count; i++) {
    if (idle_cb != nullptr) {
      idle_cb();
    }
    if (*table[i].timer == nullptr) {
      *table[i].timer = hal_soft_timer_create();
    }
    (void)hal_soft_timer_begin(*table[i].timer,
                               table[i].callback,
                               table[i].intervalMs);
    if (delay_ms > 0u && i < count - 1u) {
      extern void hal_delay_ms(uint32_t ms);
      hal_delay_ms(delay_ms);
    }
  }
  return true;
}

bool hal_soft_timer_tick_table(const hal_soft_timer_table_entry_t *table,
                               uint32_t count) {
  if (table == nullptr) {
    hal_derr("hal_soft_timer_tick_table: table is NULL");
    return false;
  }
  if (count == 0u) {
    hal_derr("hal_soft_timer_tick_table: count is 0");
    return false;
  }
  for (uint32_t i = 0u; i < count; i++) {
    hal_soft_timer_tick(*table[i].timer);
  }
  return true;
}

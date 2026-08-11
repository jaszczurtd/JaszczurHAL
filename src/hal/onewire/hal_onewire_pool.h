#ifndef JH_HAL_ONEWIRE_POOL_H
#define JH_HAL_ONEWIRE_POOL_H

#include "hal/onewire/hal_onewire.h"
#include "hal/system/hal_sync.h"

template <typename State>
static State *jh_onewire_allocate_pool_slot(State *pool, int capacity) {
  State *slot = NULL;
  hal_critical_section_enter();
  for (int i = 0; i < capacity; ++i) {
    if (!pool[i].in_use) {
      pool[i].in_use = true;
      slot = &pool[i];
      break;
    }
  }
  hal_critical_section_exit();
  HAL_ASSERT(
      slot != NULL,
      "hal_onewire: pool exhausted - increase HAL_ONEWIRE_MAX_INSTANCES");
  return slot;
}

#endif

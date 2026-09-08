#pragma once

#include "hal/system/hal_sync.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal non-asserting allocation path for status-returning modules. */
hal_mutex_t jh_hal_mutex_try_create(void);

#ifdef __cplusplus
}
#endif

/* Internal helper for singleton/bus mutexes that may be first touched by
 * concurrent tasks or RP2040 cores. The winner publishes its mutex; losers
 * destroy their private allocation and use the published handle. */
static inline hal_mutex_t jh_hal_mutex_create_once(hal_mutex_t *slot) {
  hal_mutex_t existing = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
  if (existing != NULL) {
    return existing;
  }

  hal_mutex_t created = hal_mutex_create();
  if (created == NULL) {
    return NULL;
  }

  hal_mutex_t expected = NULL;
  if (__atomic_compare_exchange_n(slot, &expected, created, false,
                                  __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
    return created;
  }

  hal_mutex_destroy(created);
  return expected;
}

/* Status-returning APIs use this variant so allocation failure can be
 * reported to their caller instead of triggering the compatibility assert in
 * hal_mutex_create(). */
static inline hal_mutex_t jh_hal_mutex_try_create_once(hal_mutex_t *slot) {
  hal_mutex_t existing = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
  if (existing != NULL) {
    return existing;
  }

  hal_mutex_t created = jh_hal_mutex_try_create();
  if (created == NULL) {
    return NULL;
  }

  hal_mutex_t expected = NULL;
  if (__atomic_compare_exchange_n(slot, &expected, created, false,
                                  __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
    return created;
  }

  hal_mutex_destroy(created);
  return expected;
}

#pragma once

#include "hal/core/hal_compiler.h"
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

/* Publish the mutex made by @p create in @p slot unless another task or core
 * got there first. The winner publishes its mutex; losers destroy their
 * private allocation and use the published handle. */
static inline hal_mutex_t
jh_hal_mutex_publish_once(hal_mutex_t *slot, hal_mutex_t (*create)(void)) {
  hal_mutex_t existing = HAL_ATOMIC_POINTER_LOAD(slot, HAL_ATOMIC_ACQUIRE);
  if (existing != NULL) {
    return existing;
  }

  hal_mutex_t created = create();
  if (created == NULL) {
    return NULL;
  }

  hal_mutex_t expected = NULL;
  /* C11/C17 forbid a failure order stronger than the success order, so the
   * publishing exchange is acq_rel rather than release (MSVC enforces it). */
  if (HAL_ATOMIC_POINTER_COMPARE_EXCHANGE(
          slot, &expected, created, HAL_ATOMIC_ACQ_REL, HAL_ATOMIC_ACQUIRE)) {
    return created;
  }

  hal_mutex_destroy(created);
  return expected;
}

/* Internal helper for singleton/bus mutexes that may be first touched by
 * concurrent tasks or RP2040 cores. */
static inline hal_mutex_t jh_hal_mutex_create_once(hal_mutex_t *slot) {
  return jh_hal_mutex_publish_once(slot, hal_mutex_create);
}

/* Status-returning APIs use this variant so allocation failure can be
 * reported to their caller instead of triggering the compatibility assert in
 * hal_mutex_create(). */
static inline hal_mutex_t jh_hal_mutex_try_create_once(hal_mutex_t *slot) {
  return jh_hal_mutex_publish_once(slot, jh_hal_mutex_try_create);
}

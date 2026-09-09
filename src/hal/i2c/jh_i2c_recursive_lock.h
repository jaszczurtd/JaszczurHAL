#pragma once

#include "hal/core/hal_mutex_once.h"

#include <stdbool.h>
#include <stdint.h>

/* Internal recursive lock shared by hardware I2C backends. The owner and
 * depth fields are atomic because contenders inspect them before taking the
 * non-recursive HAL mutex. */
typedef struct {
  hal_mutex_t mutex;
  uintptr_t owner;
  uint32_t depth;
} jh_i2c_recursive_lock_t;

static inline bool jh_i2c_recursive_lock_init(jh_i2c_recursive_lock_t *lock) {
  return lock != NULL && jh_hal_mutex_create_once(&lock->mutex) != NULL;
}

static inline bool jh_i2c_recursive_lock_acquire(jh_i2c_recursive_lock_t *lock,
                                                 uintptr_t owner) {
  if (owner == 0u || !jh_i2c_recursive_lock_init(lock)) {
    return false;
  }

  const uint32_t depth = __atomic_load_n(&lock->depth, __ATOMIC_ACQUIRE);
  const uintptr_t active_owner =
      depth > 0u ? __atomic_load_n(&lock->owner, __ATOMIC_ACQUIRE) : 0u;
  if (depth > 0u && active_owner == owner) {
    if (depth == UINT32_MAX) {
      return false;
    }
    (void)__atomic_fetch_add(&lock->depth, 1u, __ATOMIC_RELAXED);
    return true;
  }

  hal_mutex_lock(lock->mutex);
  __atomic_store_n(&lock->owner, owner, __ATOMIC_RELAXED);
  __atomic_store_n(&lock->depth, 1u, __ATOMIC_RELEASE);
  return true;
}

static inline bool jh_i2c_recursive_lock_release(jh_i2c_recursive_lock_t *lock,
                                                 uintptr_t owner) {
  if (lock == NULL || owner == 0u ||
      __atomic_load_n(&lock->mutex, __ATOMIC_ACQUIRE) == NULL) {
    return false;
  }

  const uint32_t depth = __atomic_load_n(&lock->depth, __ATOMIC_ACQUIRE);
  const uintptr_t active_owner =
      depth > 0u ? __atomic_load_n(&lock->owner, __ATOMIC_ACQUIRE) : 0u;
  if (depth == 0u || active_owner != owner) {
    return false;
  }

  if (depth > 1u) {
    (void)__atomic_fetch_sub(&lock->depth, 1u, __ATOMIC_RELEASE);
    return true;
  }

  __atomic_store_n(&lock->depth, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&lock->owner, 0u, __ATOMIC_RELAXED);
  hal_mutex_unlock(lock->mutex);
  return true;
}

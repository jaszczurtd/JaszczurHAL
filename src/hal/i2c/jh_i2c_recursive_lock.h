#pragma once

#include "hal/core/hal_compiler.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_status.h"

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

  const uint32_t depth = HAL_ATOMIC_LOAD(&lock->depth, HAL_ATOMIC_ACQUIRE);
  const uintptr_t active_owner =
      depth > 0u ? HAL_ATOMIC_LOAD(&lock->owner, HAL_ATOMIC_ACQUIRE) : 0u;
  if (depth > 0u && active_owner == owner) {
    if (depth == UINT32_MAX) {
      return false;
    }
    (void)HAL_ATOMIC_FETCH_ADD(&lock->depth, 1u, HAL_ATOMIC_RELAXED);
    return true;
  }

  hal_mutex_lock(lock->mutex);
  HAL_ATOMIC_STORE(&lock->owner, owner, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&lock->depth, 1u, HAL_ATOMIC_RELEASE);
  return true;
}

static inline bool jh_i2c_recursive_lock_release(jh_i2c_recursive_lock_t *lock,
                                                 uintptr_t owner) {
  if (lock == NULL || owner == 0u ||
      HAL_ATOMIC_POINTER_LOAD(&lock->mutex, HAL_ATOMIC_ACQUIRE) == NULL) {
    return false;
  }

  const uint32_t depth = HAL_ATOMIC_LOAD(&lock->depth, HAL_ATOMIC_ACQUIRE);
  const uintptr_t active_owner =
      depth > 0u ? HAL_ATOMIC_LOAD(&lock->owner, HAL_ATOMIC_ACQUIRE) : 0u;
  if (depth == 0u || active_owner != owner) {
    return false;
  }

  if (depth > 1u) {
    (void)HAL_ATOMIC_FETCH_SUB(&lock->depth, 1u, HAL_ATOMIC_RELEASE);
    return true;
  }

  HAL_ATOMIC_STORE(&lock->depth, 0u, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&lock->owner, 0u, HAL_ATOMIC_RELAXED);
  hal_mutex_unlock(lock->mutex);
  return true;
}

/* Read a bus field that its setter writes under the same lock. */
static inline hal_status_t
jh_i2c_recursive_lock_read_u32(jh_i2c_recursive_lock_t *lock, uintptr_t owner,
                               const uint32_t *field, uint32_t *out_value) {
  if (!jh_i2c_recursive_lock_acquire(lock, owner)) {
    return HAL_ENOMEM;
  }
  *out_value = *field;
  (void)jh_i2c_recursive_lock_release(lock, owner);
  return HAL_OK;
}

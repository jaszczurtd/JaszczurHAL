#pragma once

/**
 * @file jh_power_common.h
 * @brief Internal validation and transition ownership helpers for hal_power.
 */

#include "hal/power/hal_power.h"
#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  volatile bool busy;
} jh_power_transition_guard_t;

static inline bool jh_power_state_valid(hal_power_state_t state) {
  return state >= HAL_POWER_STATE_SLEEP && state <= HAL_POWER_STATE_POWER_DOWN;
}

static inline bool jh_power_policy_valid(hal_power_policy_t policy) {
  return policy == HAL_POWER_POLICY_FAST_WAKE ||
         policy == HAL_POWER_POLICY_LOWEST_POWER;
}

static inline bool jh_power_request_is_rtc_only(uint32_t wake_sources) {
  return (wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u &&
         (wake_sources & HAL_POWER_WAKE_SOURCE_INTERRUPT) == 0u;
}

static inline bool
jh_power_transition_claim(jh_power_transition_guard_t *guard) {
  if (guard == NULL) {
    return false;
  }
#if defined(__GNUC__) || defined(__clang__)
  return !__atomic_test_and_set(&guard->busy, __ATOMIC_ACQUIRE);
#else
  hal_critical_section_enter();
  const bool available = !guard->busy;
  if (available) {
    guard->busy = true;
  }
  hal_critical_section_exit();
  return available;
#endif
}

static inline void
jh_power_transition_release(jh_power_transition_guard_t *guard) {
  if (guard == NULL) {
    return;
  }
#if defined(__GNUC__) || defined(__clang__)
  __atomic_clear(&guard->busy, __ATOMIC_RELEASE);
#else
  hal_critical_section_enter();
  guard->busy = false;
  hal_critical_section_exit();
#endif
}

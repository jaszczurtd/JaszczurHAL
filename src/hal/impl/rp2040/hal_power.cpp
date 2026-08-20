#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_POWER_MANAGEMENT

#include "hal/power/hal_power.h"
#include "hal/power/jh_power_common.h"

#ifndef HAL_ENABLE_FREERTOS
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include "hardware/sync.h"

namespace {

#if HAL_TARGET_IS_RP2040
constexpr uint64_t kRtcResolutionUs = UINT64_C(1000000);
#else
constexpr uint64_t kRtcResolutionUs = UINT64_C(1000);
#endif
constexpr uint64_t kRtcMaximumTimeoutUs = UINT64_C(65536) * UINT64_C(1000000);
constexpr uint32_t kKnownWakeSources =
    HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT;

jh_power_transition_guard_t s_transition = {};
bool s_has_last_wake = false;
hal_power_result_t s_last_wake = {};

hal_status_t validate_request(const hal_power_request_t *request) {
  if (request == nullptr || !jh_power_state_valid(request->state) ||
      !jh_power_policy_valid(request->policy) || request->wake_sources == 0u ||
      (request->wake_sources & ~kKnownWakeSources) != 0u) {
    return HAL_EINVAL;
  }
  if (request->state != HAL_POWER_STATE_SLEEP ||
      request->policy != HAL_POWER_POLICY_FAST_WAKE ||
      (request->wake_sources &
       ~(HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT)) != 0u) {
    return HAL_EUNSUPPORTED;
  }
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    if (request->rtc == nullptr || request->rtc_timeout_us == 0u) {
      return HAL_EINVAL;
    }
    if (request->rtc_timeout_us > kRtcMaximumTimeoutUs) {
      return HAL_EOVERFLOW;
    }
  }
  return HAL_OK;
}

hal_status_t read_rtc_wakeup_pending(hal_rtc_t rtc, bool *out_pending) {
  hal_rtc_wakeup_state_t state = {};
  const hal_status_t status = hal_rtc_wakeup_get_state_ex(rtc, &state);
  if (status != HAL_OK) {
    return status;
  }
  if (!state.armed && !state.pending) {
    return HAL_ESTATE;
  }
  *out_pending = state.pending;
  return HAL_OK;
}

hal_status_t wait_for_requested_wake(const hal_power_request_t *request) {
  const bool rtc_requested =
      (request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u;
  const bool rtc_only = jh_power_request_is_rtc_only(request->wake_sources);
  bool rtc_pending = false;

  do {
    hal_status_t state_status = HAL_OK;
    bool should_wait = true;
    hal_critical_section_enter();
    if (rtc_requested) {
      state_status = read_rtc_wakeup_pending(request->rtc, &rtc_pending);
      should_wait = state_status == HAL_OK && !rtc_pending;
    }
    if (should_wait) {
      __wfi();
    }
    hal_critical_section_exit();

    if (state_status != HAL_OK) {
      return state_status;
    }
    if (rtc_only) {
      state_status = read_rtc_wakeup_pending(request->rtc, &rtc_pending);
      if (state_status != HAL_OK) {
        return state_status;
      }
    }
  } while (rtc_only && !rtc_pending);

  return HAL_OK;
}

} // namespace
#endif /* !HAL_ENABLE_FREERTOS */

hal_status_t
hal_power_get_capabilities_ex(hal_power_state_t state,
                              hal_power_capabilities_t *out_capabilities) {
  if (!jh_power_state_valid(state) || out_capabilities == nullptr) {
    return HAL_EINVAL;
  }

  hal_power_capabilities_t capabilities = {};
#ifndef HAL_ENABLE_FREERTOS
  if (state == HAL_POWER_STATE_SLEEP) {
    capabilities.supported = true;
    capabilities.resumes_execution = true;
    capabilities.retains_ram = true;
    capabilities.can_compensate_monotonic_time = true;
    capabilities.supported_policies = HAL_POWER_POLICY_MASK_FAST_WAKE;
    capabilities.wake_sources =
        HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT;
    capabilities.minimum_rtc_timeout_us = 1u;
    capabilities.maximum_rtc_timeout_us = kRtcMaximumTimeoutUs;
    capabilities.rtc_resolution_us = kRtcResolutionUs;
  }
#endif
  *out_capabilities = capabilities;
  return HAL_OK;
}

hal_status_t hal_power_enter_ex(const hal_power_request_t *request,
                                hal_power_result_t *out_result) {
#ifdef HAL_ENABLE_FREERTOS
  (void)request;
  (void)out_result;
  return HAL_EUNSUPPORTED;
#else
  const hal_status_t validation = validate_request(request);
  if (validation != HAL_OK) {
    return validation;
  }
  if (!jh_power_transition_claim(&s_transition)) {
    return HAL_EBUSY;
  }

  const uint64_t started_us = hal_micros64();
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    hal_status_t status =
        hal_rtc_wakeup_arm_ex(request->rtc, request->rtc_timeout_us, 0u);
    if (status != HAL_OK) {
      jh_power_transition_release(&s_transition);
      return status;
    }
  }

  if (request->prepare != nullptr) {
    const hal_status_t status =
        request->prepare(request->state, request->user_data);
    if (status != HAL_OK) {
      if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
        (void)hal_rtc_wakeup_cancel_ex(request->rtc);
      }
      jh_power_transition_release(&s_transition);
      return status;
    }
  }

  const hal_status_t wait_status = wait_for_requested_wake(request);
  if (wait_status != HAL_OK) {
    if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
      (void)hal_rtc_wakeup_cancel_ex(request->rtc);
    }
    jh_power_transition_release(&s_transition);
    return wait_status;
  }

  hal_power_wake_reason_t reason = HAL_POWER_WAKE_REASON_INTERRUPT;
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    uint8_t rtc_flags = 0u;
    if (hal_rtc_get_and_clear_flags_ex(request->rtc, &rtc_flags) == HAL_OK &&
        (rtc_flags & HAL_RTC_FLAG_WAKEUP) != 0u) {
      reason = HAL_POWER_WAKE_REASON_RTC;
    }
  }
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    (void)hal_rtc_wakeup_cancel_ex(request->rtc);
  }

  hal_power_result_t result = {
      request->state,
      reason,
      reason == HAL_POWER_WAKE_REASON_RTC ? HAL_POWER_WAKE_SOURCE_RTC
                                          : HAL_POWER_WAKE_SOURCE_INTERRUPT,
      hal_micros64() - started_us,
      false,
  };
  s_last_wake = result;
  s_has_last_wake = true;
  if (request->resume != nullptr) {
    request->resume(&result, request->user_data);
  }
  if (out_result != nullptr) {
    *out_result = result;
  }
  jh_power_transition_release(&s_transition);
  return HAL_OK;
#endif
}

hal_status_t hal_power_get_last_wake_ex(hal_power_result_t *out_result) {
  if (out_result == nullptr) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_FREERTOS
  return HAL_EUNSUPPORTED;
#else
  if (!s_has_last_wake) {
    return HAL_ENOENT;
  }
  *out_result = s_last_wake;
  return HAL_OK;
#endif
}

#endif /* HAL_ENABLE_POWER_MANAGEMENT */
#endif /* HAL_TARGET_IS_RP */

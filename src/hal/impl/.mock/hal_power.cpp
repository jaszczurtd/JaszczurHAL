#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_POWER_MANAGEMENT

#include "hal/impl/.mock/hal_mock.h"
#include "hal/power/hal_power.h"
#include "hal/power/jh_power_common.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

namespace {

jh_power_transition_guard_t s_transition = {};
bool s_has_last_wake = false;
hal_power_result_t s_last_wake = {};

} // namespace

hal_status_t
hal_power_get_capabilities_ex(hal_power_state_t state,
                              hal_power_capabilities_t *out_capabilities) {
  if (!jh_power_state_valid(state) || out_capabilities == nullptr) {
    return HAL_EINVAL;
  }

  hal_power_capabilities_t capabilities = {};
  capabilities.supported = true;
  capabilities.resumes_execution = state != HAL_POWER_STATE_POWER_DOWN;
  capabilities.retains_ram = state != HAL_POWER_STATE_POWER_DOWN;
  capabilities.can_compensate_monotonic_time = true;
  capabilities.supported_policies =
      HAL_POWER_POLICY_MASK_FAST_WAKE | HAL_POWER_POLICY_MASK_LOWEST_POWER;
  capabilities.wake_sources =
      HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT;
  capabilities.minimum_rtc_timeout_us = 1u;
  capabilities.maximum_rtc_timeout_us = UINT32_MAX;
  capabilities.rtc_resolution_us = 1u;
  *out_capabilities = capabilities;
  return HAL_OK;
}

hal_status_t hal_power_enter_ex(const hal_power_request_t *request,
                                hal_power_result_t *out_result) {
  if (request == nullptr || !jh_power_state_valid(request->state) ||
      !jh_power_policy_valid(request->policy) || request->wake_sources == 0u ||
      (request->wake_sources &
       ~(HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT)) != 0u) {
    return HAL_EINVAL;
  }
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u &&
      (request->rtc == nullptr || request->rtc_timeout_us == 0u)) {
    return HAL_EINVAL;
  }
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u &&
      request->rtc_timeout_us > UINT32_MAX) {
    return HAL_EOVERFLOW;
  }
  if (!jh_power_transition_claim(&s_transition)) {
    return HAL_EBUSY;
  }

  uint64_t elapsed_us = 0u;
  hal_power_wake_reason_t reason = HAL_POWER_WAKE_REASON_INTERRUPT;
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    const uint32_t rtc_flags =
        request->state == HAL_POWER_STATE_SLEEP ? 0u : HAL_RTC_WAKEUP_LOW_POWER;
    hal_status_t status =
        hal_rtc_wakeup_arm_ex(request->rtc, request->rtc_timeout_us, rtc_flags);
    if (status != HAL_OK) {
      jh_power_transition_release(&s_transition);
      return status;
    }
    hal_rtc_wakeup_state_t wakeup = {};
    status = hal_rtc_wakeup_get_state_ex(request->rtc, &wakeup);
    if (status != HAL_OK) {
      (void)hal_rtc_wakeup_cancel_ex(request->rtc);
      jh_power_transition_release(&s_transition);
      return status;
    }
    elapsed_us = wakeup.programmed_timeout_us;
  }

  if (request->prepare != nullptr) {
    const hal_status_t prepare_status =
        request->prepare(request->state, request->user_data);
    if (prepare_status != HAL_OK) {
      if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
        (void)hal_rtc_wakeup_cancel_ex(request->rtc);
      }
      jh_power_transition_release(&s_transition);
      return prepare_status;
    }
  }

  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    hal_delay_us((uint32_t)elapsed_us);
    hal_mock_rtc_fire_wakeup(request->rtc);
    reason = HAL_POWER_WAKE_REASON_RTC;
  }

  hal_power_result_t result = {
      request->state,
      reason,
      reason == HAL_POWER_WAKE_REASON_RTC ? HAL_POWER_WAKE_SOURCE_RTC
                                          : HAL_POWER_WAKE_SOURCE_INTERRUPT,
      elapsed_us,
      request->state == HAL_POWER_STATE_POWER_DOWN,
  };
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    (void)hal_rtc_wakeup_cancel_ex(request->rtc);
  }
  s_last_wake = result;
  s_has_last_wake = true;

  if (request->resume != nullptr && result.resumed_from_reset == false) {
    request->resume(&result, request->user_data);
  }
  if (out_result != nullptr) {
    *out_result = result;
  }
  jh_power_transition_release(&s_transition);
  return HAL_OK;
}

hal_status_t hal_power_get_last_wake_ex(hal_power_result_t *out_result) {
  if (out_result == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_has_last_wake) {
    return HAL_ENOENT;
  }
  *out_result = s_last_wake;
  return HAL_OK;
}

void hal_mock_power_reset(void) {
  hal_critical_section_enter();
  s_has_last_wake = false;
  s_last_wake = {};
  hal_critical_section_exit();
  jh_power_transition_release(&s_transition);
}

#endif /* HAL_ENABLE_POWER_MANAGEMENT */
#endif /* HAL_TARGET_IS_MOCK */

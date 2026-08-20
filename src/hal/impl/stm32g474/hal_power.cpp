#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_POWER_MANAGEMENT

#include "hal/power/hal_power.h"
#include "hal/power/jh_power_common.h"

#if defined(JH_STM32G474_HW) && !defined(HAL_ENABLE_FREERTOS)
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "port/stm32g474_power_port.h"
#include "port/stm32g474_regs.h"

namespace {

constexpr uint32_t kPowerWakeMagic = UINT32_C(0x4A485057); /* "JHPW" */
constexpr uint64_t kRtcResolutionUs = UINT64_C(1000000);
constexpr uint64_t kRtcMaximumTimeoutUs = UINT64_C(65536) * kRtcResolutionUs;
constexpr uint32_t kKnownWakeSources =
    HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT;

jh_power_transition_guard_t s_transition = {};
bool s_boot_wake_checked = false;
bool s_has_last_wake = false;
hal_power_result_t s_last_wake = {};

uint32_t policy_mask(hal_power_policy_t policy) {
  return policy == HAL_POWER_POLICY_FAST_WAKE
             ? HAL_POWER_POLICY_MASK_FAST_WAKE
             : HAL_POWER_POLICY_MASK_LOWEST_POWER;
}

hal_power_capabilities_t capabilities_for(hal_power_state_t state) {
  hal_power_capabilities_t capabilities = {};
  capabilities.supported = true;
  capabilities.resumes_execution = state != HAL_POWER_STATE_POWER_DOWN;
  capabilities.retains_ram = state != HAL_POWER_STATE_POWER_DOWN;
  capabilities.can_compensate_monotonic_time =
      state != HAL_POWER_STATE_POWER_DOWN;
  capabilities.minimum_rtc_timeout_us = 1u;
  capabilities.maximum_rtc_timeout_us = kRtcMaximumTimeoutUs;
  capabilities.rtc_resolution_us = kRtcResolutionUs;

  switch (state) {
  case HAL_POWER_STATE_SLEEP:
    capabilities.supported_policies = HAL_POWER_POLICY_MASK_FAST_WAKE;
    capabilities.wake_sources =
        HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT;
    break;
  case HAL_POWER_STATE_DEEP_SLEEP:
    capabilities.supported_policies =
        HAL_POWER_POLICY_MASK_FAST_WAKE | HAL_POWER_POLICY_MASK_LOWEST_POWER;
    capabilities.wake_sources =
        HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT;
    break;
  case HAL_POWER_STATE_POWER_DOWN:
    capabilities.supported_policies = HAL_POWER_POLICY_MASK_LOWEST_POWER;
    capabilities.wake_sources = HAL_POWER_WAKE_SOURCE_RTC;
    break;
  }
  return capabilities;
}

hal_status_t validate_request(const hal_power_request_t *request,
                              hal_power_capabilities_t *out_capabilities) {
  if (request == nullptr || !jh_power_state_valid(request->state) ||
      !jh_power_policy_valid(request->policy) || request->wake_sources == 0u) {
    return HAL_EINVAL;
  }
  if ((request->wake_sources & ~kKnownWakeSources) != 0u) {
    return HAL_EINVAL;
  }

  const hal_power_capabilities_t capabilities =
      capabilities_for(request->state);
  if ((capabilities.supported_policies & policy_mask(request->policy)) == 0u ||
      (request->wake_sources & ~capabilities.wake_sources) != 0u) {
    return HAL_EUNSUPPORTED;
  }
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    if (request->rtc == nullptr || request->rtc_timeout_us == 0u) {
      return HAL_EINVAL;
    }
    if (request->rtc_timeout_us > capabilities.maximum_rtc_timeout_us) {
      return HAL_EOVERFLOW;
    }
  }
  *out_capabilities = capabilities;
  return HAL_OK;
}

bool backup_access_enable(void) {
  RCC_APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
  (void)RCC_APB1ENR1;
  PWR_CR1 |= PWR_CR1_DBP;
  for (uint32_t poll = 0u; poll < UINT32_C(1000000); ++poll) {
    if ((PWR_CR1 & PWR_CR1_DBP) != 0u) {
      return true;
    }
  }
  return false;
}

} // namespace

extern "C" void stm32g474_power_capture_boot_wake(void) {
  if (s_boot_wake_checked) {
    return;
  }
  s_boot_wake_checked = true;
  if (!backup_access_enable()) {
    return;
  }

  const bool standby_wake = (PWR_SR1 & PWR_SR1_SBF) != 0u;
  const bool marked =
      TAMP_BKPR(HAL_STM32_POWER_BACKUP_REGISTER_INDEX) == kPowerWakeMagic;
  const bool rtc_wake = (RTC_SR & RTC_SR_WUTF) != 0u;

  if (marked) {
    NVIC_ICER(RTC_WKUP_IRQn / 32u) = 1u << (RTC_WKUP_IRQn % 32u);
    EXTI_IMR1 &= ~(UINT32_C(1) << 20u);
    RTC_WPR = RTC_WPR_KEY1;
    RTC_WPR = RTC_WPR_KEY2;
    RTC_CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    RTC_SCR = RTC_SCR_CWUTF;
    RTC_WPR = RTC_WPR_LOCK;
    EXTI_PR1 = UINT32_C(1) << 20u;
  }
  if (standby_wake && marked) {
    const uint64_t elapsed_seconds =
        TAMP_BKPR(HAL_STM32_POWER_TIMEOUT_BACKUP_REGISTER_INDEX);
    s_last_wake = {
        HAL_POWER_STATE_POWER_DOWN,
        rtc_wake ? HAL_POWER_WAKE_REASON_RTC : HAL_POWER_WAKE_REASON_UNKNOWN,
        rtc_wake ? HAL_POWER_WAKE_SOURCE_RTC : 0u,
        rtc_wake ? elapsed_seconds * kRtcResolutionUs : 0u,
        true,
    };
    s_has_last_wake = true;
  }
  TAMP_BKPR(HAL_STM32_POWER_BACKUP_REGISTER_INDEX) = 0u;
  TAMP_BKPR(HAL_STM32_POWER_TIMEOUT_BACKUP_REGISTER_INDEX) = 0u;
  PWR_SCR = PWR_SCR_CSBF | PWR_SCR_CWUF_MASK;
}

namespace {

void systick_suspend(uint32_t *out_control) {
  *out_control = SYSTICK_CTRL;
  SYSTICK_CTRL = *out_control & ~(SYSTICK_CTRL_ENABLE | SYSTICK_CTRL_TICKINT);
  SCB_ICSR = SCB_ICSR_PENDSTCLR;
}

void systick_resume(uint32_t control) {
  SYSTICK_LOAD = (JH_G474_CORE_CLOCK_HZ / 1000u) - 1u;
  SYSTICK_VAL = 0u;
  SCB_ICSR = SCB_ICSR_PENDSTCLR;
  SYSTICK_CTRL = control;
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

hal_status_t enter_resume_state(const hal_power_request_t *request) {
  const uint64_t suspended_at_us = hal_micros64();
  uint32_t systick_control = 0u;
  systick_suspend(&systick_control);
  const bool rtc_requested =
      (request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u;
  const bool rtc_only = jh_power_request_is_rtc_only(request->wake_sources);
  bool rtc_pending = false;
  hal_status_t state_status = HAL_OK;

  do {
    bool should_wait = true;
    hal_critical_section_enter();
    if (rtc_requested) {
      state_status = read_rtc_wakeup_pending(request->rtc, &rtc_pending);
      should_wait = state_status == HAL_OK && !rtc_pending;
    }

    if (should_wait) {
      PWR_SCR = PWR_SCR_CWUF_MASK;
      if (request->state == HAL_POWER_STATE_SLEEP) {
        SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
      } else {
        const uint32_t low_power_mode =
            request->policy == HAL_POWER_POLICY_FAST_WAKE ? PWR_CR1_LPMS_STOP0
                                                          : PWR_CR1_LPMS_STOP1;
        PWR_CR1 = (PWR_CR1 & ~PWR_CR1_LPMS_MASK) | low_power_mode;
        SCB_SCR |= SCB_SCR_SLEEPDEEP;
      }

      __asm volatile("dsb" ::: "memory");
      __asm volatile("wfi");
      __asm volatile("isb" ::: "memory");

      if (request->state == HAL_POWER_STATE_DEEP_SLEEP) {
        stm32g474_system_clock_restore_after_stop();
      }
    }
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
    hal_critical_section_exit();

    if (state_status != HAL_OK) {
      break;
    }
    if (rtc_only) {
      state_status = read_rtc_wakeup_pending(request->rtc, &rtc_pending);
    }
  } while (state_status == HAL_OK && rtc_only && !rtc_pending);

  systick_resume(systick_control);
  const uint64_t resumed_at_us = hal_micros64();
  if (resumed_at_us < suspended_at_us) {
    stm32g474_monotonic_compensate_us(suspended_at_us - resumed_at_us);
  }
  return state_status;
}

hal_power_wake_reason_t classify_wake(const hal_power_request_t *request) {
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    uint8_t rtc_flags = 0u;
    if (hal_rtc_get_and_clear_flags_ex(request->rtc, &rtc_flags) == HAL_OK &&
        (rtc_flags & HAL_RTC_FLAG_WAKEUP) != 0u) {
      return HAL_POWER_WAKE_REASON_RTC;
    }
  }
  return HAL_POWER_WAKE_REASON_INTERRUPT;
}

uint64_t rtc_elapsed_us(hal_rtc_t rtc, uint64_t epoch_before,
                        bool epoch_before_valid) {
  uint64_t epoch_after = 0u;
  if (rtc == nullptr || !epoch_before_valid ||
      hal_rtc_get_epoch_ex(rtc, &epoch_after) != HAL_OK ||
      epoch_after < epoch_before ||
      epoch_after - epoch_before > UINT64_MAX / kRtcResolutionUs) {
    return 0u;
  }
  return (epoch_after - epoch_before) * kRtcResolutionUs;
}

} // namespace
#endif /* JH_STM32G474_HW && !HAL_ENABLE_FREERTOS */

hal_status_t
hal_power_get_capabilities_ex(hal_power_state_t state,
                              hal_power_capabilities_t *out_capabilities) {
  if (!jh_power_state_valid(state) || out_capabilities == nullptr) {
    return HAL_EINVAL;
  }
#if defined(JH_STM32G474_HW) && !defined(HAL_ENABLE_FREERTOS)
  *out_capabilities = capabilities_for(state);
#else
  *out_capabilities = {};
#endif
  return HAL_OK;
}

hal_status_t hal_power_enter_ex(const hal_power_request_t *request,
                                hal_power_result_t *out_result) {
#if !defined(JH_STM32G474_HW) || defined(HAL_ENABLE_FREERTOS)
  (void)request;
  (void)out_result;
  return HAL_EUNSUPPORTED;
#else
  hal_power_capabilities_t capabilities = {};
  const hal_status_t validation = validate_request(request, &capabilities);
  (void)capabilities;
  if (validation != HAL_OK) {
    return validation;
  }
  if (!jh_power_transition_claim(&s_transition)) {
    return HAL_EBUSY;
  }

  stm32g474_power_capture_boot_wake();
  if (request->state == HAL_POWER_STATE_POWER_DOWN && !backup_access_enable()) {
    jh_power_transition_release(&s_transition);
    return HAL_EHW;
  }
  uint64_t rtc_epoch_before = 0u;
  const bool rtc_epoch_before_valid =
      request->rtc != nullptr &&
      hal_rtc_get_epoch_ex(request->rtc, &rtc_epoch_before) == HAL_OK;
  const uint64_t started_us = hal_micros64();

  hal_rtc_wakeup_state_t wakeup = {};
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    const uint32_t rtc_flags =
        request->state == HAL_POWER_STATE_SLEEP ? 0u : HAL_RTC_WAKEUP_LOW_POWER;
    hal_status_t status =
        hal_rtc_wakeup_arm_ex(request->rtc, request->rtc_timeout_us, rtc_flags);
    if (status == HAL_OK) {
      status = hal_rtc_wakeup_get_state_ex(request->rtc, &wakeup);
    }
    if (status != HAL_OK) {
      (void)hal_rtc_wakeup_cancel_ex(request->rtc);
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

  if (request->state == HAL_POWER_STATE_POWER_DOWN) {
    hal_critical_section_enter();
    hal_rtc_wakeup_state_t current_wakeup = {};
    const hal_status_t state_status =
        hal_rtc_wakeup_get_state_ex(request->rtc, &current_wakeup);
    if (state_status != HAL_OK || !current_wakeup.armed ||
        current_wakeup.pending) {
      hal_critical_section_exit();
      (void)hal_rtc_wakeup_cancel_ex(request->rtc);
      jh_power_transition_release(&s_transition);
      return state_status == HAL_OK ? HAL_EAGAIN : state_status;
    }

    TAMP_BKPR(HAL_STM32_POWER_TIMEOUT_BACKUP_REGISTER_INDEX) =
        (uint32_t)(wakeup.programmed_timeout_us / kRtcResolutionUs);
    TAMP_BKPR(HAL_STM32_POWER_BACKUP_REGISTER_INDEX) = kPowerWakeMagic;
    PWR_SCR = PWR_SCR_CSBF | PWR_SCR_CWUF_MASK;
    PWR_CR1 = (PWR_CR1 & ~PWR_CR1_LPMS_MASK) | PWR_CR1_LPMS_STANDBY;
    SCB_SCR |= SCB_SCR_SLEEPDEEP;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("wfi");
    __asm volatile("isb" ::: "memory");
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
    hal_critical_section_exit();
    TAMP_BKPR(HAL_STM32_POWER_BACKUP_REGISTER_INDEX) = 0u;
    TAMP_BKPR(HAL_STM32_POWER_TIMEOUT_BACKUP_REGISTER_INDEX) = 0u;
    (void)hal_rtc_wakeup_cancel_ex(request->rtc);
    jh_power_transition_release(&s_transition);
    return HAL_EHW;
  }

  const hal_status_t wait_status = enter_resume_state(request);
  if (wait_status != HAL_OK) {
    if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
      (void)hal_rtc_wakeup_cancel_ex(request->rtc);
    }
    jh_power_transition_release(&s_transition);
    return wait_status;
  }
  const hal_power_wake_reason_t reason = classify_wake(request);
  hal_status_t cleanup_status = HAL_OK;
  if ((request->wake_sources & HAL_POWER_WAKE_SOURCE_RTC) != 0u) {
    cleanup_status = hal_rtc_wakeup_cancel_ex(request->rtc);
  }

  const uint64_t active_elapsed_us = hal_micros64() - started_us;
  uint64_t target_elapsed_us = active_elapsed_us;
  if (reason == HAL_POWER_WAKE_REASON_RTC &&
      wakeup.programmed_timeout_us > target_elapsed_us) {
    target_elapsed_us = wakeup.programmed_timeout_us;
  } else {
    const uint64_t calendar_elapsed_us =
        rtc_elapsed_us(request->rtc, rtc_epoch_before, rtc_epoch_before_valid);
    if (calendar_elapsed_us > target_elapsed_us) {
      target_elapsed_us = calendar_elapsed_us;
    }
  }
  stm32g474_monotonic_compensate_us(target_elapsed_us - active_elapsed_us);

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
  return cleanup_status;
#endif
}

hal_status_t hal_power_get_last_wake_ex(hal_power_result_t *out_result) {
  if (out_result == nullptr) {
    return HAL_EINVAL;
  }
#if !defined(JH_STM32G474_HW) || defined(HAL_ENABLE_FREERTOS)
  return HAL_EUNSUPPORTED;
#else
  stm32g474_power_capture_boot_wake();
  if (!s_has_last_wake) {
    return HAL_ENOENT;
  }
  *out_result = s_last_wake;
  return HAL_OK;
#endif
}

#endif /* HAL_ENABLE_POWER_MANAGEMENT */
#endif /* HAL_TARGET_IS_STM32G474 */

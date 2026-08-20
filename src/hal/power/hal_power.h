#pragma once

/**
 * @file hal_power.h
 * @brief Portable processor low-power state management.
 */

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_POWER_MANAGEMENT

#include "hal/core/hal_status.h"
#include "hal/rtc/hal_rtc.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Portable low-power states ordered by increasing state loss. */
typedef enum {
  HAL_POWER_STATE_SLEEP = 0,      /**< CPU clock gated; execution resumes. */
  HAL_POWER_STATE_DEEP_SLEEP = 1, /**< Main clocks stopped; RAM retained. */
  HAL_POWER_STATE_POWER_DOWN =
      2, /**< Reset-style wake with limited retention. */
} hal_power_state_t;

/** @brief Backend policy used to resolve a portable state. */
typedef enum {
  HAL_POWER_POLICY_FAST_WAKE = 0,
  HAL_POWER_POLICY_LOWEST_POWER = 1,
} hal_power_policy_t;

#define HAL_POWER_POLICY_MASK_FAST_WAKE (UINT32_C(1) << 0u)
#define HAL_POWER_POLICY_MASK_LOWEST_POWER (UINT32_C(1) << 1u)

/** @brief Wake through the RTC supplied in @ref hal_power_request_t. */
#define HAL_POWER_WAKE_SOURCE_RTC (UINT32_C(1) << 0u)
/** @brief Wake through an enabled interrupt, including configured GPIO/EXTI. */
#define HAL_POWER_WAKE_SOURCE_INTERRUPT (UINT32_C(1) << 1u)

/** @brief Classified source of the most recent low-power wake. */
typedef enum {
  HAL_POWER_WAKE_REASON_UNKNOWN = 0,
  HAL_POWER_WAKE_REASON_RTC,
  HAL_POWER_WAKE_REASON_INTERRUPT,
} hal_power_wake_reason_t;

/** @brief Target capabilities for one portable low-power state. */
typedef struct {
  bool supported;
  bool resumes_execution;
  bool retains_ram;
  /** RTC-timed transitions can compensate the stopped monotonic source. */
  bool can_compensate_monotonic_time;
  uint32_t supported_policies;
  uint32_t wake_sources;
  uint64_t minimum_rtc_timeout_us;
  uint64_t maximum_rtc_timeout_us;
  uint64_t rtc_resolution_us;
} hal_power_capabilities_t;

/** @brief Result recorded after a low-power wake. */
typedef struct hal_power_result_s {
  hal_power_state_t state;
  hal_power_wake_reason_t reason;
  uint32_t wake_sources;
  uint64_t elapsed_us;
  bool resumed_from_reset;
} hal_power_result_t;

typedef hal_status_t (*hal_power_prepare_callback_t)(hal_power_state_t state,
                                                     void *user_data);
/** Called only when execution resumes without a reset-style wake. */
typedef void (*hal_power_resume_callback_t)(const hal_power_result_t *result,
                                            void *user_data);

/** @brief One synchronous low-power transition request. */
typedef struct {
  hal_power_state_t state;
  hal_power_policy_t policy;
  uint32_t wake_sources;
  hal_rtc_t rtc;
  uint64_t rtc_timeout_us;
  hal_power_prepare_callback_t prepare;
  hal_power_resume_callback_t resume;
  void *user_data;
} hal_power_request_t;

/** @brief Query capabilities for one portable state on the active target. */
hal_status_t
hal_power_get_capabilities_ex(hal_power_state_t state,
                              hal_power_capabilities_t *out_capabilities);

/**
 * @brief Enter a low-power state and return after a resume-style wake.
 *
 * RTC wake requests are armed immediately before entry and canceled after
 * resume. POWER_DOWN normally does not return; its result is read after boot
 * with @ref hal_power_get_last_wake_ex. A backend may return HAL_EAGAIN instead
 * of entering a reset-style state when its wake deadline has already elapsed.
 * The wake_sources mask defines which events may complete the transition;
 * unrelated enabled interrupts may be serviced, but an RTC-only request keeps
 * waiting until its RTC wake event becomes pending.
 */
hal_status_t hal_power_enter_ex(const hal_power_request_t *request,
                                hal_power_result_t *out_result);

/** @brief Read the last resume or reset-style wake result. */
hal_status_t hal_power_get_last_wake_ex(hal_power_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_POWER_MANAGEMENT */

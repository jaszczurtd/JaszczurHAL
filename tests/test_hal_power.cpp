#include "hal/impl/.mock/hal_mock.h"
#include "hal/power/hal_power.h"
#include "hal/power/jh_power_common.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

namespace {

hal_rtc_t s_rtc = nullptr;
unsigned s_prepare_calls = 0u;
unsigned s_resume_calls = 0u;
hal_status_t s_prepare_status = HAL_OK;
hal_status_t s_nested_transition_status = HAL_NONE;
hal_power_result_t s_callback_result = {};

hal_status_t prepare_callback(hal_power_state_t state, void *user_data) {
  TEST_ASSERT_EQUAL_INT(HAL_POWER_STATE_DEEP_SLEEP, state);
  TEST_ASSERT_EQUAL_PTR(&s_prepare_calls, user_data);
  ++s_prepare_calls;
  return s_prepare_status;
}

void resume_callback(const hal_power_result_t *result, void *user_data) {
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_EQUAL_PTR(&s_prepare_calls, user_data);
  ++s_resume_calls;
  s_callback_result = *result;
}

hal_status_t reentrant_prepare_callback(hal_power_state_t state,
                                        void *user_data) {
  (void)state;
  (void)user_data;
  const hal_power_request_t nested = {
      HAL_POWER_STATE_SLEEP,
      HAL_POWER_POLICY_FAST_WAKE,
      HAL_POWER_WAKE_SOURCE_INTERRUPT,
      nullptr,
      0u,
      nullptr,
      nullptr,
      nullptr,
  };
  s_nested_transition_status = hal_power_enter_ex(&nested, nullptr);
  return HAL_OK;
}

hal_power_request_t rtc_request(hal_power_state_t state, uint64_t timeout_us) {
  hal_power_request_t request = {};
  request.state = state;
  request.policy = HAL_POWER_POLICY_LOWEST_POWER;
  request.wake_sources = HAL_POWER_WAKE_SOURCE_RTC;
  request.rtc = s_rtc;
  request.rtc_timeout_us = timeout_us;
  return request;
}

} // namespace

void setUp(void) {
  hal_mock_power_reset();
  s_prepare_calls = 0u;
  s_resume_calls = 0u;
  s_prepare_status = HAL_OK;
  s_nested_transition_status = HAL_NONE;
  s_callback_result = {};

  hal_rtc_config_t config = {};
  config.chip = HAL_RTC_CHIP_INTERNAL;
  config.bus.internal.clock_source = HAL_RTC_CLOCK_SOURCE_AUTO;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&config, &s_rtc));
}

void tearDown(void) {
  hal_rtc_deinit(s_rtc);
  s_rtc = nullptr;
}

void test_power_capabilities_describe_mock_states(void) {
  for (int state = HAL_POWER_STATE_SLEEP; state <= HAL_POWER_STATE_POWER_DOWN;
       ++state) {
    hal_power_capabilities_t capabilities = {};
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, hal_power_get_capabilities_ex(
                    static_cast<hal_power_state_t>(state), &capabilities));
    TEST_ASSERT_TRUE(capabilities.supported);
    TEST_ASSERT_BITS(
        HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT,
        HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT,
        capabilities.wake_sources);
    TEST_ASSERT_EQUAL_UINT64(1u, capabilities.minimum_rtc_timeout_us);
    TEST_ASSERT_EQUAL_UINT64(UINT32_MAX, capabilities.maximum_rtc_timeout_us);
  }

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_power_get_capabilities_ex(
                            static_cast<hal_power_state_t>(99), nullptr));
}

void test_power_common_identifies_rtc_only_wake_requests(void) {
  TEST_ASSERT_TRUE(jh_power_request_is_rtc_only(HAL_POWER_WAKE_SOURCE_RTC));
  TEST_ASSERT_FALSE(jh_power_request_is_rtc_only(
      HAL_POWER_WAKE_SOURCE_RTC | HAL_POWER_WAKE_SOURCE_INTERRUPT));
  TEST_ASSERT_FALSE(
      jh_power_request_is_rtc_only(HAL_POWER_WAKE_SOURCE_INTERRUPT));
}

void test_power_rtc_wake_advances_monotonic_and_runs_callbacks(void) {
  hal_power_request_t request =
      rtc_request(HAL_POWER_STATE_DEEP_SLEEP, UINT64_C(2500001));
  request.prepare = prepare_callback;
  request.resume = resume_callback;
  request.user_data = &s_prepare_calls;

  const uint64_t before_us = hal_micros64();
  hal_power_result_t result = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_power_enter_ex(&request, &result));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(2500001), hal_micros64() - before_us);
  TEST_ASSERT_EQUAL_INT(HAL_POWER_WAKE_REASON_RTC, result.reason);
  TEST_ASSERT_EQUAL_HEX32(HAL_POWER_WAKE_SOURCE_RTC, result.wake_sources);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(2500001), result.elapsed_us);
  TEST_ASSERT_FALSE(result.resumed_from_reset);
  TEST_ASSERT_EQUAL_UINT(1u, s_prepare_calls);
  TEST_ASSERT_EQUAL_UINT(1u, s_resume_calls);
  TEST_ASSERT_EQUAL_INT(result.reason, s_callback_result.reason);

  hal_power_result_t last = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_power_get_last_wake_ex(&last));
  TEST_ASSERT_EQUAL_UINT64(result.elapsed_us, last.elapsed_us);

  hal_rtc_wakeup_state_t wakeup = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_wakeup_get_state_ex(s_rtc, &wakeup));
  TEST_ASSERT_FALSE(wakeup.armed);
  TEST_ASSERT_FALSE(wakeup.pending);
}

void test_power_interrupt_wake_is_immediate(void) {
  hal_power_request_t request = {};
  request.state = HAL_POWER_STATE_SLEEP;
  request.policy = HAL_POWER_POLICY_FAST_WAKE;
  request.wake_sources = HAL_POWER_WAKE_SOURCE_INTERRUPT;

  hal_power_result_t result = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_power_enter_ex(&request, &result));
  TEST_ASSERT_EQUAL_INT(HAL_POWER_WAKE_REASON_INTERRUPT, result.reason);
  TEST_ASSERT_EQUAL_HEX32(HAL_POWER_WAKE_SOURCE_INTERRUPT, result.wake_sources);
  TEST_ASSERT_EQUAL_UINT64(0u, result.elapsed_us);
}

void test_power_down_mock_models_reset_style_result(void) {
  hal_power_request_t request =
      rtc_request(HAL_POWER_STATE_POWER_DOWN, UINT64_C(10));
  request.resume = resume_callback;
  request.user_data = &s_prepare_calls;
  hal_power_result_t result = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_power_enter_ex(&request, &result));
  TEST_ASSERT_TRUE(result.resumed_from_reset);
  TEST_ASSERT_EQUAL_INT(HAL_POWER_STATE_POWER_DOWN, result.state);
  TEST_ASSERT_EQUAL_UINT(0u, s_resume_calls);
}

void test_power_prepare_failure_cancels_armed_wakeup(void) {
  hal_power_request_t request =
      rtc_request(HAL_POWER_STATE_DEEP_SLEEP, UINT64_C(100));
  request.prepare = prepare_callback;
  request.user_data = &s_prepare_calls;
  s_prepare_status = HAL_ECANCELED;

  TEST_ASSERT_EQUAL_INT(HAL_ECANCELED, hal_power_enter_ex(&request, nullptr));
  hal_rtc_wakeup_state_t wakeup = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_wakeup_get_state_ex(s_rtc, &wakeup));
  TEST_ASSERT_FALSE(wakeup.armed);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_power_get_last_wake_ex(&s_callback_result));
}

void test_power_rejects_reentrant_transition(void) {
  hal_power_request_t request =
      rtc_request(HAL_POWER_STATE_DEEP_SLEEP, UINT64_C(10));
  request.prepare = reentrant_prepare_callback;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_power_enter_ex(&request, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, s_nested_transition_status);
}

void test_power_rejects_invalid_requests(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_power_enter_ex(nullptr, nullptr));

  hal_power_request_t request = rtc_request(HAL_POWER_STATE_SLEEP, 0u);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_power_enter_ex(&request, nullptr));
  request.rtc_timeout_us = static_cast<uint64_t>(UINT32_MAX) + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_power_enter_ex(&request, nullptr));
  request.rtc_timeout_us = 1u;
  request.rtc = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_power_enter_ex(&request, nullptr));
  request.rtc = s_rtc;
  request.policy = static_cast<hal_power_policy_t>(99);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_power_enter_ex(&request, nullptr));
  request.policy = HAL_POWER_POLICY_LOWEST_POWER;
  request.wake_sources = UINT32_C(0x80000000);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_power_enter_ex(&request, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_power_get_last_wake_ex(nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_power_capabilities_describe_mock_states);
  RUN_TEST(test_power_common_identifies_rtc_only_wake_requests);
  RUN_TEST(test_power_rtc_wake_advances_monotonic_and_runs_callbacks);
  RUN_TEST(test_power_interrupt_wake_is_immediate);
  RUN_TEST(test_power_down_mock_models_reset_style_result);
  RUN_TEST(test_power_prepare_failure_cancels_armed_wakeup);
  RUN_TEST(test_power_rejects_reentrant_transition);
  RUN_TEST(test_power_rejects_invalid_requests);
  return UNITY_END();
}

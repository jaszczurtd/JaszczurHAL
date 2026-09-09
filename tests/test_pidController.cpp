#include "hal/impl/.mock/hal_mock.h"
#include "utils/pidController.h"
#include "utils/unity.h"

void setUp(void) { hal_mock_set_millis(0); }

void tearDown(void) {}

void test_proportional_output(void) {
  PIDController pid(1.0f, 0.0f, 0.0f, 0.0f);
  pid.setOutputLimits(-1000.0f, 1000.0f);
  pid.updatePIDtime(1.0f);
  float out = pid.updatePIDcontroller(5.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, out);
}

void test_zero_error_zero_output(void) {
  PIDController pid(1.0f, 0.0f, 0.0f, 0.0f);
  pid.setOutputLimits(-1000.0f, 1000.0f);
  pid.updatePIDtime(1.0f);
  float out = pid.updatePIDcontroller(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out);
}

void test_output_clamped_to_max(void) {
  PIDController pid(10.0f, 0.0f, 0.0f, 0.0f);
  pid.setOutputLimits(-50.0f, 50.0f);
  pid.updatePIDtime(1.0f);
  float out = pid.updatePIDcontroller(100.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, out);
}

void test_output_clamped_to_min(void) {
  PIDController pid(10.0f, 0.0f, 0.0f, 0.0f);
  pid.setOutputLimits(-50.0f, 50.0f);
  pid.updatePIDtime(1.0f);
  float out = pid.updatePIDcontroller(-100.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -50.0f, out);
}

void test_reset_clears_integral(void) {
  PIDController pid(0.0f, 1.0f, 0.0f, 100.0f);
  pid.setOutputLimits(-1000.0f, 1000.0f);
  pid.updatePIDtime(1.0f);
  pid.updatePIDcontroller(10.0f);
  pid.reset();
  float out = pid.updatePIDcontroller(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out);
}

void test_gains_set_and_get(void) {
  PIDController pid;
  pid.setKp(1.5f);
  pid.setKi(0.5f);
  pid.setKd(0.1f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, pid.getKp());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, pid.getKi());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, pid.getKd());
}

void test_error_stable_within_tolerance(void) {
  PIDController pid(1.0f, 0.0f, 0.0f, 0.0f);
  pid.setOutputLimits(-1000.0f, 1000.0f);
  pid.updatePIDtime(1.0f);
  /* isErrorStable() itself increments the stability counter - call it 3 times
   */
  bool stable = false;
  for (int i = 0; i < 3; i++) {
    pid.updatePIDcontroller(0.1f);
    stable = pid.isErrorStable(0.1f, 0.5f, 3);
  }
  TEST_ASSERT_TRUE(stable);
}

static float legacy_step_after(PIDController &pid, uint32_t elapsed_ms,
                               float divider = 1000.0f, float error = 1000.0f) {
  hal_mock_advance_millis(elapsed_ms);
  pid.updatePIDtime(divider);
  return pid.updatePIDcontroller(error);
}

void test_legacy_time_keeps_resolution_after_float_timestamp_limit(void) {
  hal_mock_set_millis(
      UINT32_C(16777216)); // 2^24 ms: float loses odd milliseconds.
  PIDController pid(0, 1, 0, 1000);
  for (unsigned i = 1; i <= 16; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(.001f, 5.0f * i, legacy_step_after(pid, 5));
  }
}

void test_default_constructor_starts_time_at_creation(void) {
  hal_mock_set_millis(123456);
  PIDController pid;
  pid.setKi(1);
  pid.setMaxIntegral(1000);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 5, legacy_step_after(pid, 5));
}

void test_c_constructors_start_time_at_creation(void) {
  hal_mock_set_millis(123456);
  hal_pid_controller_t controllers[] = {
      hal_pid_controller_create(),
      hal_pid_controller_create_with_gains(0, 1, 0, 1000)};
  hal_mock_advance_millis(5);
  float outputs[COUNTOF(controllers)] = {};
  for (unsigned i = 0; i < COUNTOF(controllers); ++i) {
    hal_pid_controller_set_ki(controllers[i], 1);
    hal_pid_controller_set_max_integral(controllers[i], 1000);
    hal_pid_controller_update_time(controllers[i], 1000);
    outputs[i] = hal_pid_controller_update(controllers[i], 1000);
    hal_pid_controller_destroy(controllers[i]);
  }
  for (float output : outputs) {
    TEST_ASSERT_FLOAT_WITHIN(.001f, 5, output);
  }
}

void test_legacy_time_handles_millisecond_counter_wrap(void) {
  hal_mock_set_millis(UINT32_MAX - 2U);
  PIDController pid(0, 1, 0, 1000);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 5, legacy_step_after(pid, 5));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 10, legacy_step_after(pid, 5));
}

void test_reset_restarts_time_without_float_rounding(void) {
  PIDController pid(0, 1, 0, 1000);
  (void)legacy_step_after(pid, 10);
  hal_mock_set_millis(UINT32_C(16777217));
  pid.reset();
  TEST_ASSERT_FLOAT_WITHIN(.001f, 5, legacy_step_after(pid, 5));
}

void test_legacy_time_preserves_divider_and_fallback(void) {
  PIDController pid(0, 1, 0, 1000);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 5, legacy_step_after(pid, 5));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 55, legacy_step_after(pid, 5, 100));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 56, legacy_step_after(pid, 5, 0));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 57, legacy_step_after(pid, 0));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 62, legacy_step_after(pid, 5));
}

static void check_signed_antiwindup(float ki, Direction direction,
                                    bool explicit_time) {
  PIDController pid(0, ki, 0, 1000);
  pid.setDirection(direction);
  pid.setOutputLimits(-10, 10);
  const float limits[] = {10, -10};
  for (float limit : limits) {
    pid.reset();
    const float outward_error = limit / pid.getKi();
    for (unsigned i = 0; i <= 100; ++i) {
      const float error = (i < 100) ? outward_error : -outward_error / 2;
      const float expected = (i < 100) ? limit : limit / 2;
      float output;
      if (explicit_time) {
        hal_pid_terms_t terms = {};
        TEST_ASSERT_EQUAL(HAL_OK, pid.step(error, 0, 1, 0, &terms));
        output = terms.output;
      } else {
        output = legacy_step_after(pid, 1000, 1000, error);
      }
      TEST_ASSERT_FLOAT_WITHIN(.001f, expected, output);
    }
  }
}

void test_legacy_antiwindup_positive_ki(void) {
  check_signed_antiwindup(1, FORWARD, false);
}

void test_legacy_antiwindup_negative_ki(void) {
  check_signed_antiwindup(-1, FORWARD, false);
}

void test_legacy_antiwindup_backward(void) {
  check_signed_antiwindup(1, BACKWARD, false);
  check_signed_antiwindup(-1, BACKWARD, false);
}

void test_explicit_antiwindup_handles_gain_and_direction_signs(void) {
  check_signed_antiwindup(1, FORWARD, true);
  check_signed_antiwindup(-1, FORWARD, true);
  check_signed_antiwindup(1, BACKWARD, true);
  check_signed_antiwindup(-1, BACKWARD, true);
}

static float integrate_for_one_second(unsigned steps) {
  PIDController pid(0.0f, 0.4f, 0.0f, 1000.0f);
  hal_pid_terms_t terms = {};
  for (unsigned i = 0; i < steps; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK,
                      pid.step(100.0f, 0.0f, 1.0f / steps, 0.0f, &terms));
  }
  return terms.integral;
}

void test_explicit_time_is_independent_of_clock_and_step_count(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, integrate_for_one_second(200));
  hal_mock_set_millis(UINT32_C(16777217));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, integrate_for_one_second(4000));
  hal_mock_set_millis(UINT32_MAX);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, integrate_for_one_second(200));
}

void test_measurement_derivative_ignores_setpoint_steps(void) {
  PIDController pid(1.0f, 0.0f, 0.1f, 0.0f);
  hal_pid_terms_t terms = {};
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(0, 500, .005f, 0, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, terms.derivative);
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(100, 500, .005f, 0, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, terms.derivative);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 100, terms.output);
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(90, 510, .005f, 0, &terms));
  TEST_ASSERT_TRUE(terms.derivative < 0);
  pid.reset();
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(0, 1000, .005f, 0, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, terms.derivative);
}

void test_integral_deadband_preserves_proportional_response(void) {
  PIDController pid(1, 1, 0, 1000);
  hal_pid_terms_t terms = {};
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(-20, 0, .01f, 40, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, -20, terms.proportional);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, terms.integral);
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(50, 0, .01f, 40, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, .1f, terms.integral);
}

void test_current_saturation_blocks_windup_and_allows_recovery(void) {
  PIDController pid(0, 1, 0, 1000);
  pid.setOutputLimits(-10, 10);
  hal_pid_terms_t terms = {};
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(10, 0, 1, 0, &terms));
  TEST_ASSERT_TRUE(terms.saturated_high);
  for (unsigned i = 0; i < 100; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, pid.step(10, 0, 1, 0, &terms));
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 10, terms.integral);
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(0, 0, 1, 0, &terms));
  TEST_ASSERT_TRUE(terms.saturated_high);
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(-5, 0, 1, 0, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 5, terms.output);
  TEST_ASSERT_FALSE(terms.saturated_high);
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(-15, 0, 1, 0, &terms));
  TEST_ASSERT_TRUE(terms.saturated_low);
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(-15, 0, 1, 0, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, -10, terms.integral);
}

void test_invalid_step_preserves_integral_and_derivative_state(void) {
  PIDController pid(1, 1, .1f, 1000);
  hal_pid_terms_t terms = {};
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(10, 20, .01f, 0, &terms));
  const float output = terms.output;
  TEST_ASSERT_EQUAL(HAL_EINVAL, pid.step(10, 300, 0, 0, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, output, terms.output);
  TEST_ASSERT_EQUAL(HAL_EINVAL, pid.step(NAN, 300, .01f, 0, &terms));
  TEST_ASSERT_EQUAL(HAL_EINVAL, pid.step(10, 300, .01f, 0, nullptr));
  TEST_ASSERT_EQUAL(HAL_EOVERFLOW, pid.step(FLT_MAX, 300, FLT_MAX, 0, &terms));
  TEST_ASSERT_EQUAL(HAL_OK, pid.step(0, 20, .01f, 0, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.001f, .1f, terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, terms.derivative);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_proportional_output);
  RUN_TEST(test_zero_error_zero_output);
  RUN_TEST(test_output_clamped_to_max);
  RUN_TEST(test_output_clamped_to_min);
  RUN_TEST(test_reset_clears_integral);
  RUN_TEST(test_gains_set_and_get);
  RUN_TEST(test_error_stable_within_tolerance);
  RUN_TEST(test_legacy_time_keeps_resolution_after_float_timestamp_limit);
  RUN_TEST(test_default_constructor_starts_time_at_creation);
  RUN_TEST(test_c_constructors_start_time_at_creation);
  RUN_TEST(test_legacy_time_handles_millisecond_counter_wrap);
  RUN_TEST(test_reset_restarts_time_without_float_rounding);
  RUN_TEST(test_legacy_time_preserves_divider_and_fallback);
  RUN_TEST(test_legacy_antiwindup_positive_ki);
  RUN_TEST(test_legacy_antiwindup_negative_ki);
  RUN_TEST(test_legacy_antiwindup_backward);
  RUN_TEST(test_explicit_antiwindup_handles_gain_and_direction_signs);
  RUN_TEST(test_explicit_time_is_independent_of_clock_and_step_count);
  RUN_TEST(test_measurement_derivative_ignores_setpoint_steps);
  RUN_TEST(test_integral_deadband_preserves_proportional_response);
  RUN_TEST(test_current_saturation_blocks_windup_and_allows_recovery);
  RUN_TEST(test_invalid_step_preserves_integral_and_derivative_state);
  return UNITY_END();
}

#include "utils/unity.h"
#include "utils/pidController.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_set_millis(0);
}

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
    /* isErrorStable() itself increments the stability counter - call it 3 times */
    bool stable = false;
    for (int i = 0; i < 3; i++) {
        pid.updatePIDcontroller(0.1f);
        stable = pid.isErrorStable(0.1f, 0.5f, 3);
    }
    TEST_ASSERT_TRUE(stable);
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
    return UNITY_END();
}

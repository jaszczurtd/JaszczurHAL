#include "utils/unity.h"
#include "hal/hal_timer.h"
#include "hal/impl/.mock/hal_mock.h"

static int s_fired;
static hal_alarm_id_t s_fired_id;

static int64_t on_alarm(hal_alarm_id_t id, void *) {
    s_fired++;
    s_fired_id = id;
    return 0;
}

void setUp(void) {
    s_fired = 0;
    s_fired_id = HAL_ALARM_INVALID;
    hal_mock_timer_reset();
}

void tearDown(void) {}

void test_alarm_fires_after_advance(void) {
    hal_timer_add_alarm_us(1000, on_alarm, nullptr, false);
    hal_mock_timer_advance_us(1000);
    TEST_ASSERT_EQUAL_INT(1, s_fired);
}

void test_alarm_does_not_fire_before_delay(void) {
    hal_timer_add_alarm_us(1000, on_alarm, nullptr, false);
    hal_mock_timer_advance_us(999);
    TEST_ASSERT_EQUAL_INT(0, s_fired);
}

void test_alarm_fires_exactly_at_delay(void) {
    hal_timer_add_alarm_us(500, on_alarm, nullptr, false);
    hal_mock_timer_advance_us(500);
    TEST_ASSERT_EQUAL_INT(1, s_fired);
}

void test_cancel_prevents_firing(void) {
    hal_alarm_id_t id = hal_timer_add_alarm_us(1000, on_alarm, nullptr, false);
    TEST_ASSERT_TRUE(hal_timer_cancel_alarm(id));
    hal_mock_timer_advance_us(2000);
    TEST_ASSERT_EQUAL_INT(0, s_fired);
}

void test_cancel_invalid_id_returns_false(void) {
    TEST_ASSERT_FALSE(hal_timer_cancel_alarm(HAL_ALARM_INVALID));
}

void test_multiple_alarms_fire_in_order(void) {
    hal_timer_add_alarm_us(100, on_alarm, nullptr, false);
    hal_timer_add_alarm_us(200, on_alarm, nullptr, false);
    hal_mock_timer_advance_us(150);
    TEST_ASSERT_EQUAL_INT(1, s_fired);
    hal_mock_timer_advance_us(100);
    TEST_ASSERT_EQUAL_INT(2, s_fired);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_alarm_fires_after_advance);
    RUN_TEST(test_alarm_does_not_fire_before_delay);
    RUN_TEST(test_alarm_fires_exactly_at_delay);
    RUN_TEST(test_cancel_prevents_firing);
    RUN_TEST(test_cancel_invalid_id_returns_false);
    RUN_TEST(test_multiple_alarms_fire_in_order);
    return UNITY_END();
}

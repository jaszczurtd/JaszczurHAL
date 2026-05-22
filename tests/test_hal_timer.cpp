#include "utils/unity.h"
#include "hal/hal_timer.h"
#include "hal/impl/.mock/hal_mock.h"

static int s_fired;
static hal_alarm_id_t s_fired_id;
static int s_managed_fired;

static int64_t on_alarm(hal_alarm_id_t id, void *) {
    s_fired++;
    s_fired_id = id;
    return 0;
}

static void on_managed_timer(hal_timer_t, void *) {
    s_managed_fired++;
}

void setUp(void) {
    s_fired = 0;
    s_fired_id = HAL_ALARM_INVALID;
    s_managed_fired = 0;
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

void test_pool_alarm_fires_after_advance(void) {
    hal_timer_pool_t pool = hal_timer_pool_create_auto(4);
    TEST_ASSERT_NOT_NULL(pool);

    hal_alarm_id_t id = hal_timer_pool_add_alarm_us(pool, 1000, on_alarm, nullptr, false);
    TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id);

    hal_mock_timer_advance_us(1000);
    TEST_ASSERT_EQUAL_INT(1, s_fired);

    hal_timer_pool_destroy(pool);
}

void test_pool_cancel_is_pool_scoped(void) {
    hal_timer_pool_t pool_a = hal_timer_pool_create_auto(4);
    hal_timer_pool_t pool_b = hal_timer_pool_create_auto(4);
    TEST_ASSERT_NOT_NULL(pool_a);
    TEST_ASSERT_NOT_NULL(pool_b);

    hal_alarm_id_t id_a = hal_timer_pool_add_alarm_us(pool_a, 1000, on_alarm, nullptr, false);
    hal_alarm_id_t id_b = hal_timer_pool_add_alarm_us(pool_b, 1000, on_alarm, nullptr, false);
    TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id_a);
    TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id_b);

    TEST_ASSERT_FALSE(hal_timer_pool_cancel_alarm(pool_b, id_a));
    TEST_ASSERT_TRUE(hal_timer_pool_cancel_alarm(pool_a, id_a));

    hal_mock_timer_advance_us(1000);
    TEST_ASSERT_EQUAL_INT(1, s_fired);
    TEST_ASSERT_EQUAL_INT(id_b, s_fired_id);

    hal_timer_pool_destroy(pool_a);
    hal_timer_pool_destroy(pool_b);
}

void test_pool_destroy_cancels_pending_alarms(void) {
    hal_timer_pool_t pool = hal_timer_pool_create_auto(4);
    TEST_ASSERT_NOT_NULL(pool);

    hal_alarm_id_t id = hal_timer_pool_add_alarm_us(pool, 1000, on_alarm, nullptr, false);
    TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id);

    hal_timer_pool_destroy(pool);
    hal_mock_timer_advance_us(2000);
    TEST_ASSERT_EQUAL_INT(0, s_fired);
}

void test_add_alarm_ex_reports_invalid_arg(void) {
    hal_timer_result_t res = HAL_TIMER_OK;
    hal_alarm_id_t id = hal_timer_add_alarm_us_ex(1000, nullptr, nullptr, false, &res);
    TEST_ASSERT_EQUAL_INT(HAL_ALARM_INVALID, id);
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_ERR_INVALID_ARG, (int)res);
}

void test_pool_add_alarm_ex_reports_pool_full(void) {
    hal_timer_pool_t pool = hal_timer_pool_create_auto(1);
    TEST_ASSERT_NOT_NULL(pool);

    hal_timer_result_t res1 = HAL_TIMER_ERR_INTERNAL;
    hal_timer_result_t res2 = HAL_TIMER_ERR_INTERNAL;

    hal_alarm_id_t id1 = hal_timer_pool_add_alarm_us_ex(pool, 1000, on_alarm, nullptr, false, &res1);
    hal_alarm_id_t id2 = hal_timer_pool_add_alarm_us_ex(pool, 1000, on_alarm, nullptr, false, &res2);

    TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id1);
    TEST_ASSERT_EQUAL_INT(HAL_ALARM_INVALID, id2);
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, (int)res1);
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_ERR_POOL_FULL, (int)res2);

    hal_timer_pool_destroy(pool);
}

void test_managed_timer_stop_prevents_fire(void) {
    hal_timer_t timer = nullptr;
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK,
                          hal_timer_create(HAL_TIMER_POOL_DEFAULT,
                                           100,
                                           false,
                                           on_managed_timer,
                                           nullptr,
                                           &timer));
    TEST_ASSERT_NOT_NULL(timer);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_start(timer));
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_stop(timer));

    hal_mock_timer_advance_us(1000);
    TEST_ASSERT_EQUAL_INT(0, s_managed_fired);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_destroy(timer));
}

void test_managed_periodic_pause_and_resume(void) {
    hal_timer_t timer = nullptr;
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK,
                          hal_timer_create(HAL_TIMER_POOL_DEFAULT,
                                           100,
                                           true,
                                           on_managed_timer,
                                           nullptr,
                                           &timer));
    TEST_ASSERT_NOT_NULL(timer);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_start(timer));
    hal_mock_timer_advance_us(100);
    TEST_ASSERT_EQUAL_INT(1, s_managed_fired);

    int64_t rem_before_pause = -1;
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_get_remaining_us(timer, &rem_before_pause));
    TEST_ASSERT_TRUE(rem_before_pause > 0);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_pause(timer));
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_STATE_PAUSED, hal_timer_get_state(timer));

    int64_t rem_paused = -1;
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_get_remaining_us(timer, &rem_paused));
    TEST_ASSERT_TRUE(rem_paused > 0);

    hal_mock_timer_advance_us(5000);
    TEST_ASSERT_EQUAL_INT(1, s_managed_fired);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_resume(timer));
    hal_mock_timer_advance_us((uint64_t)rem_paused - 1u);
    TEST_ASSERT_EQUAL_INT(1, s_managed_fired);
    hal_mock_timer_advance_us(1u);
    TEST_ASSERT_EQUAL_INT(2, s_managed_fired);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_destroy(timer));
}

void test_managed_set_period_with_restart(void) {
    hal_timer_t timer = nullptr;
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK,
                          hal_timer_create(HAL_TIMER_POOL_DEFAULT,
                                           200,
                                           true,
                                           on_managed_timer,
                                           nullptr,
                                           &timer));
    TEST_ASSERT_NOT_NULL(timer);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_start(timer));
    hal_mock_timer_advance_us(100);
    TEST_ASSERT_EQUAL_INT(0, s_managed_fired);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_set_period_us(timer, 50, true));
    hal_mock_timer_advance_us(49);
    TEST_ASSERT_EQUAL_INT(0, s_managed_fired);
    hal_mock_timer_advance_us(1);
    TEST_ASSERT_EQUAL_INT(1, s_managed_fired);

    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_destroy(timer));
}

void test_managed_get_remaining_stopped_returns_error(void) {
    hal_timer_t timer = nullptr;
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK,
                          hal_timer_create(HAL_TIMER_POOL_DEFAULT,
                                           100,
                                           false,
                                           on_managed_timer,
                                           nullptr,
                                           &timer));
    TEST_ASSERT_NOT_NULL(timer);

    int64_t rem = -1;
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_ERR_NOT_RUNNING, hal_timer_get_remaining_us(timer, &rem));
    TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_destroy(timer));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_alarm_fires_after_advance);
    RUN_TEST(test_alarm_does_not_fire_before_delay);
    RUN_TEST(test_alarm_fires_exactly_at_delay);
    RUN_TEST(test_cancel_prevents_firing);
    RUN_TEST(test_cancel_invalid_id_returns_false);
    RUN_TEST(test_multiple_alarms_fire_in_order);
    RUN_TEST(test_pool_alarm_fires_after_advance);
    RUN_TEST(test_pool_cancel_is_pool_scoped);
    RUN_TEST(test_pool_destroy_cancels_pending_alarms);
    RUN_TEST(test_add_alarm_ex_reports_invalid_arg);
    RUN_TEST(test_pool_add_alarm_ex_reports_pool_full);
    RUN_TEST(test_managed_timer_stop_prevents_fire);
    RUN_TEST(test_managed_periodic_pause_and_resume);
    RUN_TEST(test_managed_set_period_with_restart);
    RUN_TEST(test_managed_get_remaining_stopped_returns_error);
    return UNITY_END();
}

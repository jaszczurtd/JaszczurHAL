#include "utils/unity.h"
#include "hal/hal_soft_timer.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int s_cb_a_count;
static int s_cb_b_count;
static int s_cb_c_count;
static int s_idle_count;

static void cb_a(void) { s_cb_a_count++; }
static void cb_b(void) { s_cb_b_count++; }
static void cb_c(void) { s_cb_c_count++; }
static void idle_cb(void) { s_idle_count++; }

/* SmartTimers treats _lastTime==0 as "uninitialized" sentinel, so all tests
   start the mock clock at 1000 ms to avoid that guard. */
static const uint32_t T0 = 1000;

void setUp(void) {
    s_cb_a_count = 0;
    s_cb_b_count = 0;
    s_cb_c_count = 0;
    s_idle_count = 0;
    hal_mock_set_millis(T0);
    hal_mock_watchdog_reset_flag();
}

void tearDown(void) {}

/* ── basic single-timer API tests ────────────────────────────────────────── */

void test_create_returns_non_null(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    TEST_ASSERT_NOT_NULL(t);
    hal_soft_timer_destroy(t);
}

void test_destroy_null_is_safe(void) {
    hal_soft_timer_destroy(NULL);
}

void test_begin_null_returns_false(void) {
    TEST_ASSERT_FALSE(hal_soft_timer_begin(NULL, cb_a, 100));
}

void test_begin_and_tick_fires_callback(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    hal_soft_timer_begin(t, cb_a, 100);
    hal_mock_set_millis(T0 + 100);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(1, s_cb_a_count);
    hal_soft_timer_destroy(t);
}

void test_tick_does_not_fire_before_interval(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    hal_soft_timer_begin(t, cb_a, 100);
    hal_mock_set_millis(T0 + 99);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(0, s_cb_a_count);
    hal_soft_timer_destroy(t);
}

void test_tick_null_is_safe(void) {
    hal_soft_timer_tick(NULL);
}

void test_available_returns_true_after_interval(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    hal_soft_timer_begin(t, cb_a, 50);
    hal_mock_set_millis(T0 + 50);
    TEST_ASSERT_TRUE(hal_soft_timer_available(t));
    hal_soft_timer_destroy(t);
}

void test_available_null_returns_false(void) {
    TEST_ASSERT_FALSE(hal_soft_timer_available(NULL));
}

void test_abort_prevents_callback(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    hal_soft_timer_begin(t, cb_a, 100);
    hal_soft_timer_abort(t);
    hal_mock_set_millis(T0 + 200);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(0, s_cb_a_count);
    hal_soft_timer_destroy(t);
}

void test_restart_resets_timer(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    hal_soft_timer_begin(t, cb_a, 100);
    hal_mock_set_millis(T0 + 50);
    hal_soft_timer_restart(t);
    /* 99 ms after restart -> not yet */
    hal_mock_set_millis(T0 + 149);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(0, s_cb_a_count);
    /* 100 ms after restart -> fire */
    hal_mock_set_millis(T0 + 150);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(1, s_cb_a_count);
    hal_soft_timer_destroy(t);
}

void test_set_interval_changes_period(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    hal_soft_timer_begin(t, cb_a, 100);
    hal_mock_set_millis(T0 + 100);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(1, s_cb_a_count);
    /* change to 200 ms */
    hal_soft_timer_set_interval(t, 200);
    hal_mock_set_millis(T0 + 200);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(1, s_cb_a_count);  /* only 100 ms since last tick */
    hal_mock_set_millis(T0 + 300);
    hal_soft_timer_tick(t);
    TEST_ASSERT_EQUAL_INT(2, s_cb_a_count);
    hal_soft_timer_destroy(t);
}

void test_time_left_returns_remaining(void) {
    hal_soft_timer_t t = hal_soft_timer_create();
    hal_soft_timer_begin(t, cb_a, 100);
    hal_mock_set_millis(T0 + 30);
    uint32_t left = hal_soft_timer_time_left(t);
    TEST_ASSERT_EQUAL_UINT32(70, left);
    hal_soft_timer_destroy(t);
}

void test_time_left_null_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, hal_soft_timer_time_left(NULL));
}

/* ── setup_table tests ───────────────────────────────────────────────────── */

void test_setup_table_creates_timers(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_t tb = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 },
        { &tb, cb_b, 200 }
    };

    TEST_ASSERT_TRUE(hal_soft_timer_setup_table(table, 2, NULL, 0));

    TEST_ASSERT_NOT_NULL(ta);
    TEST_ASSERT_NOT_NULL(tb);

    hal_soft_timer_destroy(ta);
    hal_soft_timer_destroy(tb);
}

void test_setup_table_timers_fire_at_correct_intervals(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_t tb = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 },
        { &tb, cb_b, 200 }
    };

    hal_soft_timer_setup_table(table, 2, NULL, 0);

    hal_mock_set_millis(T0 + 100);
    hal_soft_timer_tick(ta);
    hal_soft_timer_tick(tb);
    TEST_ASSERT_EQUAL_INT(1, s_cb_a_count);
    TEST_ASSERT_EQUAL_INT(0, s_cb_b_count);

    hal_mock_set_millis(T0 + 200);
    hal_soft_timer_tick(ta);
    hal_soft_timer_tick(tb);
    TEST_ASSERT_EQUAL_INT(2, s_cb_a_count);
    TEST_ASSERT_EQUAL_INT(1, s_cb_b_count);

    hal_soft_timer_destroy(ta);
    hal_soft_timer_destroy(tb);
}

void test_setup_table_calls_idle_callback(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_t tb = NULL;
    hal_soft_timer_t tc = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 },
        { &tb, cb_b, 200 },
        { &tc, cb_c, 300 }
    };

    hal_soft_timer_setup_table(table, 3, idle_cb, 0);

    /* idle_cb called once per entry = 3 times */
    TEST_ASSERT_EQUAL_INT(3, s_idle_count);

    hal_soft_timer_destroy(ta);
    hal_soft_timer_destroy(tb);
    hal_soft_timer_destroy(tc);
}

void test_setup_table_inserts_delay_between_entries(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_t tb = NULL;
    hal_soft_timer_t tc = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 },
        { &tb, cb_b, 200 },
        { &tc, cb_c, 300 }
    };

    uint32_t before = hal_millis();
    hal_soft_timer_setup_table(table, 3, NULL, 10);
    uint32_t after = hal_millis();

    /* delay between entries (not after last one): 2 * 10 = 20 ms */
    TEST_ASSERT_EQUAL_UINT32(20, after - before);

    hal_soft_timer_destroy(ta);
    hal_soft_timer_destroy(tb);
    hal_soft_timer_destroy(tc);
}

void test_setup_table_no_delay_for_single_entry(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 }
    };

    uint32_t before = hal_millis();
    hal_soft_timer_setup_table(table, 1, NULL, 10);
    uint32_t after = hal_millis();

    TEST_ASSERT_EQUAL_UINT32(0, after - before);

    hal_soft_timer_destroy(ta);
}

void test_setup_table_null_table_is_safe(void) {
    TEST_ASSERT_FALSE(hal_soft_timer_setup_table(NULL, 5, idle_cb, 10));
    TEST_ASSERT_EQUAL_INT(0, s_idle_count);
}

void test_setup_table_zero_count_is_noop(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 }
    };

    TEST_ASSERT_FALSE(hal_soft_timer_setup_table(table, 0, idle_cb, 10));
    TEST_ASSERT_NULL(ta);
    TEST_ASSERT_EQUAL_INT(0, s_idle_count);
}

void test_setup_table_does_not_recreate_existing_timer(void) {
    hal_soft_timer_t ta = hal_soft_timer_create();
    hal_soft_timer_t original = ta;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 }
    };

    hal_soft_timer_setup_table(table, 1, NULL, 0);

    /* timer handle should be unchanged - not recreated */
    TEST_ASSERT_EQUAL_PTR(original, ta);

    hal_soft_timer_destroy(ta);
}

void test_setup_table_with_watchdog_feed(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_t tb = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 },
        { &tb, cb_b, 200 }
    };

    hal_mock_watchdog_reset_flag();
    hal_soft_timer_setup_table(table, 2, hal_watchdog_feed, 5);
    TEST_ASSERT_TRUE(hal_mock_watchdog_was_fed());

    hal_soft_timer_destroy(ta);
    hal_soft_timer_destroy(tb);
}

/* ── tick_table tests ────────────────────────────────────────────────────── */

void test_tick_table_fires_all_elapsed(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_t tb = NULL;
    hal_soft_timer_t tc = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 },
        { &tb, cb_b, 100 },
        { &tc, cb_c, 200 }
    };

    hal_soft_timer_setup_table(table, 3, NULL, 0);

    hal_mock_set_millis(T0 + 100);
    hal_soft_timer_tick_table(table, 3);
    TEST_ASSERT_EQUAL_INT(1, s_cb_a_count);
    TEST_ASSERT_EQUAL_INT(1, s_cb_b_count);
    TEST_ASSERT_EQUAL_INT(0, s_cb_c_count);

    hal_mock_set_millis(T0 + 200);
    hal_soft_timer_tick_table(table, 3);
    TEST_ASSERT_EQUAL_INT(2, s_cb_a_count);
    TEST_ASSERT_EQUAL_INT(2, s_cb_b_count);
    TEST_ASSERT_EQUAL_INT(1, s_cb_c_count);

    hal_soft_timer_destroy(ta);
    hal_soft_timer_destroy(tb);
    hal_soft_timer_destroy(tc);
}

void test_tick_table_null_is_safe(void) {
    TEST_ASSERT_FALSE(hal_soft_timer_tick_table(NULL, 5));
}

void test_tick_table_zero_count_is_noop(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 100 }
    };
    hal_soft_timer_setup_table(table, 1, NULL, 0);
    hal_mock_set_millis(T0 + 100);
    TEST_ASSERT_FALSE(hal_soft_timer_tick_table(table, 0));
    TEST_ASSERT_EQUAL_INT(0, s_cb_a_count);
    hal_soft_timer_destroy(ta);
}

void test_tick_table_repeated_ticks(void) {
    hal_soft_timer_t ta = NULL;
    hal_soft_timer_table_entry_t table[] = {
        { &ta, cb_a, 50 }
    };

    hal_soft_timer_setup_table(table, 1, NULL, 0);

    for (int i = 1; i <= 5; i++) {
        hal_mock_set_millis(T0 + (uint32_t)(i * 50));
        hal_soft_timer_tick_table(table, 1);
    }
    TEST_ASSERT_EQUAL_INT(5, s_cb_a_count);

    hal_soft_timer_destroy(ta);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void) {
    UNITY_BEGIN();

    /* basic single-timer API */
    RUN_TEST(test_create_returns_non_null);
    RUN_TEST(test_destroy_null_is_safe);
    RUN_TEST(test_begin_null_returns_false);
    RUN_TEST(test_begin_and_tick_fires_callback);
    RUN_TEST(test_tick_does_not_fire_before_interval);
    RUN_TEST(test_tick_null_is_safe);
    RUN_TEST(test_available_returns_true_after_interval);
    RUN_TEST(test_available_null_returns_false);
    RUN_TEST(test_abort_prevents_callback);
    RUN_TEST(test_restart_resets_timer);
    RUN_TEST(test_set_interval_changes_period);
    RUN_TEST(test_time_left_returns_remaining);
    RUN_TEST(test_time_left_null_returns_zero);

    /* setup_table */
    RUN_TEST(test_setup_table_creates_timers);
    RUN_TEST(test_setup_table_timers_fire_at_correct_intervals);
    RUN_TEST(test_setup_table_calls_idle_callback);
    RUN_TEST(test_setup_table_inserts_delay_between_entries);
    RUN_TEST(test_setup_table_no_delay_for_single_entry);
    RUN_TEST(test_setup_table_null_table_is_safe);
    RUN_TEST(test_setup_table_zero_count_is_noop);
    RUN_TEST(test_setup_table_does_not_recreate_existing_timer);
    RUN_TEST(test_setup_table_with_watchdog_feed);

    /* tick_table */
    RUN_TEST(test_tick_table_fires_all_elapsed);
    RUN_TEST(test_tick_table_null_is_safe);
    RUN_TEST(test_tick_table_zero_count_is_noop);
    RUN_TEST(test_tick_table_repeated_ticks);

    return UNITY_END();
}

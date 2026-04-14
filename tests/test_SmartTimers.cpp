#include "utils/unity.h"
#include "utils/SmartTimers.h"
#include "hal/impl/.mock/hal_mock.h"

static int s_cb_count;
static void on_tick(void) { s_cb_count++; }

/* SmartTimers treats _lastTime==0 as "uninitialized" sentinel, so all tests
   start the mock clock at 1000 ms to avoid that guard. */
static const uint32_t T0 = 1000;

void setUp(void) {
    s_cb_count = 0;
    hal_mock_set_millis(T0);
}

void tearDown(void) {}

void test_not_available_before_interval(void) {
    SmartTimers t;
    t.begin(on_tick, 100);
    hal_mock_set_millis(T0 + 99);
    TEST_ASSERT_FALSE(t.available());
}

void test_available_after_interval(void) {
    SmartTimers t;
    t.begin(on_tick, 100);
    hal_mock_set_millis(T0 + 100);
    TEST_ASSERT_TRUE(t.available());
}

void test_tick_fires_callback(void) {
    SmartTimers t;
    t.begin(on_tick, 100);
    hal_mock_set_millis(T0 + 100);
    t.tick();
    TEST_ASSERT_EQUAL_INT(1, s_cb_count);
}

void test_tick_fires_multiple_times(void) {
    SmartTimers t;
    t.begin(on_tick, 100);
    hal_mock_set_millis(T0 + 100); t.tick();
    hal_mock_set_millis(T0 + 200); t.tick();
    hal_mock_set_millis(T0 + 300); t.tick();
    TEST_ASSERT_EQUAL_INT(3, s_cb_count);
}

void test_abort_stops_callback(void) {
    SmartTimers t;
    t.begin(on_tick, 100);
    t.abort();
    hal_mock_set_millis(T0 + 100); t.tick();
    TEST_ASSERT_EQUAL_INT(0, s_cb_count);
}

void test_restart_resets_timer(void) {
    SmartTimers t;
    t.begin(on_tick, 100);
    hal_mock_set_millis(T0 + 50);
    t.restart();
    hal_mock_set_millis(T0 + 149); t.tick();
    TEST_ASSERT_EQUAL_INT(0, s_cb_count);
    hal_mock_set_millis(T0 + 150); t.tick();
    TEST_ASSERT_EQUAL_INT(1, s_cb_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_not_available_before_interval);
    RUN_TEST(test_available_after_interval);
    RUN_TEST(test_tick_fires_callback);
    RUN_TEST(test_tick_fires_multiple_times);
    RUN_TEST(test_abort_stops_callback);
    RUN_TEST(test_restart_resets_timer);
    return UNITY_END();
}

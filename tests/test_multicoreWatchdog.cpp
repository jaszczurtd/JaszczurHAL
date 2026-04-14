#include "utils/unity.h"
#include "utils/multicoreWatchdog.h"
#include "hal/impl/.mock/hal_mock.h"

/*
 * multicoreWatchdog.cpp depends on tools.cpp for this symbol.
 * For host tests we provide a no-op stub.
 */
void saveLoggerAndClose(void) {}

static const uint32_t T0 = 1000;
static const uint32_t WATCHDOG_MS = 1000;
static const uint32_t TICK_MS = WATCHDOG_MS / 10;

void setUp(void) {
    hal_mock_set_caused_reboot(false);
    hal_mock_set_millis(T0);
    hal_mock_watchdog_reset_flag();
}

void tearDown(void) {}

void test_update_before_setup_does_not_feed(void) {
    updateWatchdogCore0();
    updateWatchdogCore1();
    TEST_ASSERT_FALSE(hal_mock_watchdog_was_fed());
}

void test_feeds_only_when_both_cores_report_progress(void) {
    setupWatchdog(NULL, WATCHDOG_MS);
    hal_mock_watchdog_reset_flag();

    hal_mock_advance_millis(TICK_MS / 2);
    updateWatchdogCore0();
    updateWatchdogCore1();

    hal_mock_advance_millis(TICK_MS / 2);
    updateWatchdogCore0();

    TEST_ASSERT_TRUE(hal_mock_watchdog_was_fed());
}

void test_single_core_progress_does_not_feed(void) {
    setupWatchdog(NULL, WATCHDOG_MS);
    hal_mock_watchdog_reset_flag();

    hal_mock_advance_millis(TICK_MS);
    updateWatchdogCore0();
    TEST_ASSERT_FALSE(hal_mock_watchdog_was_fed());

    hal_mock_advance_millis(TICK_MS);
    updateWatchdogCore0();
    TEST_ASSERT_FALSE(hal_mock_watchdog_was_fed());
}

void test_external_reset_blocks_watchdog_feed(void) {
    setupWatchdog(NULL, WATCHDOG_MS);
    hal_mock_watchdog_reset_flag();

    triggerSystemReset();

    hal_mock_advance_millis(TICK_MS);
    updateWatchdogCore0();
    updateWatchdogCore1();

    TEST_ASSERT_FALSE(hal_mock_watchdog_was_fed());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_update_before_setup_does_not_feed);
    RUN_TEST(test_feeds_only_when_both_cores_report_progress);
    RUN_TEST(test_single_core_progress_does_not_feed);
    RUN_TEST(test_external_reset_blocks_watchdog_feed);
    return UNITY_END();
}

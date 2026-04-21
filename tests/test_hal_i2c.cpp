#include "utils/unity.h"
#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_i2c_deinit();
    hal_i2c_deinit_bus(1);
    hal_i2c_init(4, 5, 400000);
    hal_mock_i2c_set_busy(false);
    hal_mock_i2c_set_busy_bus(1, false);
}

void tearDown(void) {}

void test_begin_transmission_sets_last_address(void) {
    hal_i2c_begin_transmission(0x3C);
    TEST_ASSERT_EQUAL_UINT8(0x3C, hal_mock_i2c_get_last_addr());
}

void test_request_from_and_read_sequence(void) {
    const uint8_t rx[] = {0x10, 0x20, 0x30};
    hal_mock_i2c_inject_rx(rx, 3);

    TEST_ASSERT_EQUAL_UINT8(3, hal_i2c_request_from(0x48, 3));
    TEST_ASSERT_EQUAL_INT(3, hal_i2c_available());
    TEST_ASSERT_EQUAL_INT(0x10, hal_i2c_read());
    TEST_ASSERT_EQUAL_INT(0x20, hal_i2c_read());
    TEST_ASSERT_EQUAL_INT(0x30, hal_i2c_read());
    TEST_ASSERT_EQUAL_INT(0, hal_i2c_available());
    TEST_ASSERT_EQUAL_INT(-1, hal_i2c_read());
}

void test_write_and_end_transmission_return_success(void) {
    hal_i2c_begin_transmission(0x50);
    TEST_ASSERT_EQUAL_UINT(1, hal_i2c_write(0xAB));
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
}

void test_is_busy_reflects_mock_state(void) {
    hal_mock_i2c_set_busy(false);
    TEST_ASSERT_FALSE(hal_i2c_is_busy(0x67));

    hal_mock_i2c_set_busy(true);
    TEST_ASSERT_TRUE(hal_i2c_is_busy(0x67));
}

void test_bus1_api_independent_state(void) {
    hal_i2c_init_bus(1, 6, 7, 100000);
    hal_i2c_begin_transmission(0x3C);
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
    hal_i2c_begin_transmission_bus(1, 0x52);
    TEST_ASSERT_EQUAL_UINT8(0x52, hal_mock_i2c_get_last_addr_bus(1));
    TEST_ASSERT_EQUAL_UINT8(0x3C, hal_mock_i2c_get_last_addr_bus(0));

    const uint8_t rx1[] = {0xAA, 0xBB};
    hal_mock_i2c_inject_rx_bus(1, rx1, 2);
    TEST_ASSERT_EQUAL_UINT8(2, hal_i2c_request_from_bus(1, 0x52, 2));
    TEST_ASSERT_EQUAL_INT(2, hal_i2c_available_bus(1));
    TEST_ASSERT_EQUAL_INT(0xAA, hal_i2c_read_bus(1));
    TEST_ASSERT_EQUAL_INT(0xBB, hal_i2c_read_bus(1));
    TEST_ASSERT_EQUAL_INT(-1, hal_i2c_read_bus(1));

    hal_mock_i2c_set_busy_bus(1, true);
    TEST_ASSERT_TRUE(hal_i2c_is_busy_bus(1, 0x52));
    hal_mock_i2c_set_busy_bus(1, false);
    TEST_ASSERT_FALSE(hal_i2c_is_busy_bus(1, 0x52));
}

void test_manual_lock_unlock_tracks_depth_per_bus(void) {
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));

    hal_i2c_lock();
    hal_i2c_lock_bus(1);
    hal_i2c_lock_bus(1);

    TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth_bus(0));
    TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_lock_depth_bus(1));

    hal_i2c_unlock_bus(1);
    hal_i2c_unlock();
    hal_i2c_unlock_bus(1);
    hal_i2c_unlock_bus(1); // underflow-safe no-op

    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));
}

void test_transmission_calls_balance_lock_depth(void) {
    hal_i2c_begin_transmission(0x3C);
    TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth_bus(0));
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));

    hal_i2c_begin_transmission_bus(1, 0x52);
    TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth_bus(1));
    TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission_bus(1));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));
}

void test_request_from_balances_lock_depth(void) {
    const uint8_t rx0[] = {0x01};
    const uint8_t rx1[] = {0xAA};
    hal_mock_i2c_inject_rx(rx0, 1);
    hal_mock_i2c_inject_rx_bus(1, rx1, 1);

    TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_request_from(0x48, 1));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));

    TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_request_from_bus(1, 0x49, 1));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));
}

void test_deinit_marks_bus_uninitialized(void) {
    TEST_ASSERT_TRUE(hal_mock_i2c_is_initialized());
    TEST_ASSERT_FALSE(hal_mock_i2c_is_initialized_bus(1));

    hal_i2c_init_bus(1, 6, 7, 100000);
    TEST_ASSERT_TRUE(hal_mock_i2c_is_initialized_bus(1));

    hal_i2c_deinit();
    TEST_ASSERT_FALSE(hal_mock_i2c_is_initialized());

    hal_i2c_deinit_bus(1);
    TEST_ASSERT_FALSE(hal_mock_i2c_is_initialized_bus(1));
}

/* ── Transaction count ─────────────────────────────────────────────────────── */

void test_transaction_count_starts_at_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, hal_i2c_get_transaction_count());
}

void test_transaction_count_increments_on_write(void) {
    hal_i2c_begin_transmission(0x50);
    hal_i2c_write(0xAA);
    hal_i2c_end_transmission();
    TEST_ASSERT_EQUAL_UINT32(1, hal_i2c_get_transaction_count());
    hal_i2c_begin_transmission(0x50);
    hal_i2c_end_transmission();
    TEST_ASSERT_EQUAL_UINT32(2, hal_i2c_get_transaction_count());
}

void test_transaction_count_increments_on_request(void) {
    const uint8_t rx[] = {0x01};
    hal_mock_i2c_inject_rx(rx, 1);
    hal_i2c_request_from(0x48, 1);
    TEST_ASSERT_EQUAL_UINT32(1, hal_i2c_get_transaction_count());
}

void test_transaction_count_resets_on_init(void) {
    hal_i2c_begin_transmission(0x50);
    hal_i2c_end_transmission();
    TEST_ASSERT_TRUE(hal_i2c_get_transaction_count() > 0);
    hal_i2c_init(4, 5, 400000);
    TEST_ASSERT_EQUAL_UINT32(0, hal_i2c_get_transaction_count());
}

void test_transaction_count_bus_independence(void) {
    hal_i2c_init_bus(1, 6, 7, 100000);
    hal_i2c_begin_transmission(0x50);
    hal_i2c_end_transmission();  /* bus 0 */
    TEST_ASSERT_EQUAL_UINT32(1, hal_i2c_get_transaction_count());
    TEST_ASSERT_EQUAL_UINT32(0, hal_i2c_get_transaction_count_bus(1));
}

/* ── Bus clear ─────────────────────────────────────────────────────────────── */

void test_bus_clear_increments_count(void) {
    TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_bus_clear_count());
    hal_i2c_bus_clear(4, 5);
    TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_bus_clear_count());
    hal_i2c_bus_clear(4, 5);
    TEST_ASSERT_EQUAL_UINT32(2, hal_mock_i2c_get_bus_clear_count());
}

void test_bus_clear_count_resets_on_init(void) {
    hal_i2c_bus_clear(4, 5);
    TEST_ASSERT_TRUE(hal_mock_i2c_get_bus_clear_count() > 0);
    hal_i2c_init(4, 5, 400000);
    TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_bus_clear_count());
}

void test_bus_clear_bus_independence(void) {
    hal_i2c_init_bus(1, 6, 7, 100000);
    hal_i2c_bus_clear(4, 5);
    hal_i2c_bus_clear_bus(1, 6, 7);
    hal_i2c_bus_clear_bus(1, 6, 7);
    TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_bus_clear_count());
    TEST_ASSERT_EQUAL_UINT32(2, hal_mock_i2c_get_bus_clear_count_bus(1));
}

// ── hal_i2c_write_byte convenience helper ───────────────────────────────────

void test_write_byte_performs_begin_write_end_sequence(void) {
    hal_mock_i2c_set_busy(false);
    bool writeOk = false;
    uint8_t status = hal_i2c_write_byte(0x42, 0xA5, &writeOk);
    TEST_ASSERT_EQUAL_UINT8(0, status);
    TEST_ASSERT_TRUE(writeOk);
    TEST_ASSERT_EQUAL_UINT8(0x42, hal_mock_i2c_get_last_addr());
}

void test_write_byte_returns_end_tx_error_when_bus_busy(void) {
    hal_mock_i2c_set_busy(true);
    bool writeOk = false;
    uint8_t status = hal_i2c_write_byte(0x55, 0x11, &writeOk);
    TEST_ASSERT_NOT_EQUAL(0, status);
    TEST_ASSERT_TRUE(writeOk);  // the queue-one-byte step itself succeeded
    hal_mock_i2c_set_busy(false);
}

void test_write_byte_accepts_null_out_flag(void) {
    hal_mock_i2c_set_busy(false);
    uint8_t status = hal_i2c_write_byte(0x21, 0x00, NULL);
    TEST_ASSERT_EQUAL_UINT8(0, status);
    TEST_ASSERT_EQUAL_UINT8(0x21, hal_mock_i2c_get_last_addr());
}

void test_write_byte_balances_lock_depth(void) {
    hal_mock_i2c_set_busy(false);
    int before = hal_mock_i2c_get_lock_depth();
    hal_i2c_write_byte(0x3C, 0x7F, NULL);
    TEST_ASSERT_EQUAL_INT(before, hal_mock_i2c_get_lock_depth());
}

void test_write_byte_bus_routes_to_selected_controller(void) {
    hal_i2c_init_bus(1, 6, 7, 100000);
    hal_mock_i2c_set_busy_bus(1, false);

    bool writeOk = false;
    uint8_t status = hal_i2c_write_byte_bus(1, 0x68, 0x5A, &writeOk);

    TEST_ASSERT_EQUAL_UINT8(0, status);
    TEST_ASSERT_TRUE(writeOk);
    TEST_ASSERT_EQUAL_UINT8(0x68, hal_mock_i2c_get_last_addr_bus(1));
    // The helper must not touch bus 0.
    TEST_ASSERT_NOT_EQUAL(0x68, hal_mock_i2c_get_last_addr_bus(0));
}

void test_write_byte_increments_transaction_count(void) {
    hal_mock_i2c_set_busy(false);
    uint32_t before = hal_i2c_get_transaction_count();
    hal_i2c_write_byte(0x30, 0x01, NULL);
    TEST_ASSERT_EQUAL_UINT32(before + 1, hal_i2c_get_transaction_count());
}

// ── hal_i2c_read_byte convenience helper ────────────────────────────────────

void test_read_byte_returns_injected_value(void) {
    const uint8_t rx[] = {0xA5};
    hal_mock_i2c_inject_rx(rx, 1);

    bool readOk = false;
    uint8_t value = hal_i2c_read_byte(0x48, &readOk);

    TEST_ASSERT_EQUAL_UINT8(0xA5, value);
    TEST_ASSERT_TRUE(readOk);
}

void test_read_byte_preserves_zero_value(void) {
    // Verify a genuine 0x00 is reported as success (not confused with failure).
    const uint8_t rx[] = {0x00};
    hal_mock_i2c_inject_rx(rx, 1);

    bool readOk = false;
    uint8_t value = hal_i2c_read_byte(0x22, &readOk);

    TEST_ASSERT_EQUAL_UINT8(0x00, value);
    TEST_ASSERT_TRUE(readOk);
}

void test_read_byte_accepts_null_out_flag(void) {
    const uint8_t rx[] = {0x7E};
    hal_mock_i2c_inject_rx(rx, 1);

    uint8_t value = hal_i2c_read_byte(0x3C, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x7E, value);
}

void test_read_byte_balances_lock_depth(void) {
    const uint8_t rx[] = {0x42};
    hal_mock_i2c_inject_rx(rx, 1);
    int before = hal_mock_i2c_get_lock_depth();
    (void)hal_i2c_read_byte(0x55, NULL);
    TEST_ASSERT_EQUAL_INT(before, hal_mock_i2c_get_lock_depth());
}

void test_read_byte_bus_routes_to_selected_controller(void) {
    hal_i2c_init_bus(1, 6, 7, 100000);
    // Inject different bytes on each bus so a misrouted read returns the
    // wrong value. hal_i2c_request_from_bus() does not update cur_addr, so
    // we can't assert via hal_mock_i2c_get_last_addr_bus() here.
    const uint8_t rx0[] = {0x11};
    const uint8_t rx1[] = {0x9F};
    hal_mock_i2c_inject_rx(rx0, 1);
    hal_mock_i2c_inject_rx_bus(1, rx1, 1);

    bool readOk = false;
    uint8_t value = hal_i2c_read_byte_bus(1, 0x68, &readOk);

    TEST_ASSERT_EQUAL_UINT8(0x9F, value);
    TEST_ASSERT_TRUE(readOk);
}

void test_read_byte_increments_transaction_count(void) {
    const uint8_t rx[] = {0x01};
    hal_mock_i2c_inject_rx(rx, 1);
    uint32_t before = hal_i2c_get_transaction_count();
    (void)hal_i2c_read_byte(0x30, NULL);
    TEST_ASSERT_EQUAL_UINT32(before + 1, hal_i2c_get_transaction_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_transmission_sets_last_address);
    RUN_TEST(test_request_from_and_read_sequence);
    RUN_TEST(test_write_and_end_transmission_return_success);
    RUN_TEST(test_is_busy_reflects_mock_state);
    RUN_TEST(test_bus1_api_independent_state);
    RUN_TEST(test_manual_lock_unlock_tracks_depth_per_bus);
    RUN_TEST(test_transmission_calls_balance_lock_depth);
    RUN_TEST(test_request_from_balances_lock_depth);
    RUN_TEST(test_deinit_marks_bus_uninitialized);
    RUN_TEST(test_transaction_count_starts_at_zero);
    RUN_TEST(test_transaction_count_increments_on_write);
    RUN_TEST(test_transaction_count_increments_on_request);
    RUN_TEST(test_transaction_count_resets_on_init);
    RUN_TEST(test_transaction_count_bus_independence);
    RUN_TEST(test_bus_clear_increments_count);
    RUN_TEST(test_bus_clear_count_resets_on_init);
    RUN_TEST(test_bus_clear_bus_independence);
    RUN_TEST(test_write_byte_performs_begin_write_end_sequence);
    RUN_TEST(test_write_byte_returns_end_tx_error_when_bus_busy);
    RUN_TEST(test_write_byte_accepts_null_out_flag);
    RUN_TEST(test_write_byte_balances_lock_depth);
    RUN_TEST(test_write_byte_bus_routes_to_selected_controller);
    RUN_TEST(test_write_byte_increments_transaction_count);
    RUN_TEST(test_read_byte_returns_injected_value);
    RUN_TEST(test_read_byte_preserves_zero_value);
    RUN_TEST(test_read_byte_accepts_null_out_flag);
    RUN_TEST(test_read_byte_balances_lock_depth);
    RUN_TEST(test_read_byte_bus_routes_to_selected_controller);
    RUN_TEST(test_read_byte_increments_transaction_count);
    return UNITY_END();
}

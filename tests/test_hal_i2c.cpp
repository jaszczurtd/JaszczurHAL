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
    return UNITY_END();
}

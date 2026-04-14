#include "utils/unity.h"
#include "hal/hal_spi.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_spi_reset();
}

void tearDown(void) {}

void test_spi_not_initialized_after_reset(void) {
    TEST_ASSERT_FALSE(hal_mock_spi_is_initialized());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_bus());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_rx_pin());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_tx_pin());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_sck_pin());
}

void test_spi_init_stores_bus_and_pins(void) {
    hal_spi_init(1, 12, 13, 14);

    TEST_ASSERT_TRUE(hal_mock_spi_is_initialized());
    TEST_ASSERT_EQUAL_UINT8(1, hal_mock_spi_get_bus());
    TEST_ASSERT_EQUAL_UINT8(12, hal_mock_spi_get_rx_pin());
    TEST_ASSERT_EQUAL_UINT8(13, hal_mock_spi_get_tx_pin());
    TEST_ASSERT_EQUAL_UINT8(14, hal_mock_spi_get_sck_pin());
}

void test_spi_reinit_overwrites_previous_values(void) {
    hal_spi_init(0, 4, 5, 6);
    hal_spi_init(1, 20, 21, 22);

    TEST_ASSERT_TRUE(hal_mock_spi_is_initialized());
    TEST_ASSERT_EQUAL_UINT8(1, hal_mock_spi_get_bus());
    TEST_ASSERT_EQUAL_UINT8(20, hal_mock_spi_get_rx_pin());
    TEST_ASSERT_EQUAL_UINT8(21, hal_mock_spi_get_tx_pin());
    TEST_ASSERT_EQUAL_UINT8(22, hal_mock_spi_get_sck_pin());
}

void test_spi_reset_clears_state(void) {
    hal_spi_init(1, 9, 10, 11);
    TEST_ASSERT_TRUE(hal_mock_spi_is_initialized());

    hal_mock_spi_reset();

    TEST_ASSERT_FALSE(hal_mock_spi_is_initialized());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_bus());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_rx_pin());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_tx_pin());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_sck_pin());
}

void test_spi_lock_unlock_tracks_per_bus_depth(void) {
    TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1));

    hal_spi_lock(0);
    hal_spi_lock(1);
    hal_spi_lock(1);

    TEST_ASSERT_EQUAL_INT(1, hal_mock_spi_get_lock_depth(0));
    TEST_ASSERT_EQUAL_INT(2, hal_mock_spi_get_lock_depth(1));

    hal_spi_unlock(1);
    hal_spi_unlock(0);
    hal_spi_unlock(1);

    TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_spi_not_initialized_after_reset);
    RUN_TEST(test_spi_init_stores_bus_and_pins);
    RUN_TEST(test_spi_reinit_overwrites_previous_values);
    RUN_TEST(test_spi_reset_clears_state);
    RUN_TEST(test_spi_lock_unlock_tracks_per_bus_depth);
    return UNITY_END();
}

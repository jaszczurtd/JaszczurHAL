#include "utils/unity.h"
#include "hal/hal_spi.h"
#include "hal/impl/.mock/hal_mock.h"
#include <SPI.h>

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

void test_spi_transaction_settings_are_stored_per_bus(void) {
    hal_spi_settings_t settings = {10000000u, HAL_SPI_LSBFIRST, HAL_SPI_MODE3};

    hal_spi_begin_transaction(1, &settings);

    TEST_ASSERT_TRUE(hal_mock_spi_transaction_active(1));
    TEST_ASSERT_EQUAL_UINT32(10000000u, hal_mock_spi_get_clock_hz(1));
    TEST_ASSERT_EQUAL_UINT8(HAL_SPI_LSBFIRST, hal_mock_spi_get_bit_order(1));
    TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE3, hal_mock_spi_get_data_mode(1));

    hal_spi_end_transaction(1);
    TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1));
}

void test_spi_transfer_records_tx_and_reads_scripted_rx(void) {
    uint8_t scripted_rx[] = {0x11, 0x22, 0x33};
    uint8_t buffer[] = {0xBB, 0xCC};
    uint8_t tx_log[4] = {};

    hal_mock_spi_push_rx(0, scripted_rx, sizeof(scripted_rx));

    uint8_t first = hal_spi_transfer(0, 0xAA);
    hal_spi_transfer_buffer(0, buffer, sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT8(0x11, first);
    TEST_ASSERT_EQUAL_UINT8(0x22, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x33, buffer[1]);
    TEST_ASSERT_EQUAL_UINT32(3u, hal_mock_spi_get_transfer_count(0));
    TEST_ASSERT_EQUAL_size_t(3u, hal_mock_spi_get_tx(0, tx_log, sizeof(tx_log)));
    TEST_ASSERT_EQUAL_UINT8(0xAA, tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, tx_log[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, tx_log[2]);
}

void test_spi_transfer16_respects_active_bit_order(void) {
    hal_spi_settings_t msb = {4000000u, HAL_SPI_MSBFIRST, HAL_SPI_MODE0};
    uint8_t msb_rx[] = {0x12, 0x34};
    uint8_t tx_log[4] = {};

    hal_spi_begin_transaction(0, &msb);
    hal_mock_spi_push_rx(0, msb_rx, sizeof(msb_rx));

    TEST_ASSERT_EQUAL_UINT16(0x1234, hal_spi_transfer16(0, 0xABCD));
    TEST_ASSERT_EQUAL_size_t(2u, hal_mock_spi_get_tx(0, tx_log, sizeof(tx_log)));
    TEST_ASSERT_EQUAL_UINT8(0xAB, tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, tx_log[1]);

    hal_mock_spi_reset();

    hal_spi_settings_t lsb = {4000000u, HAL_SPI_LSBFIRST, HAL_SPI_MODE0};
    uint8_t lsb_rx[] = {0x78, 0x56};
    hal_spi_begin_transaction(0, &lsb);
    hal_mock_spi_push_rx(0, lsb_rx, sizeof(lsb_rx));

    TEST_ASSERT_EQUAL_UINT16(0x5678, hal_spi_transfer16(0, 0xBEEF));
    TEST_ASSERT_EQUAL_size_t(2u, hal_mock_spi_get_tx(0, tx_log, sizeof(tx_log)));
    TEST_ASSERT_EQUAL_UINT8(0xEF, tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBE, tx_log[1]);
}

void test_spi_transfer_txrx_supports_full_duplex_buffers(void) {
    uint8_t tx[] = {0x01, 0x02, 0x03};
    uint8_t scripted_rx[] = {0xA1, 0xA2, 0xA3};
    uint8_t rx[3] = {};
    uint8_t tx_log[3] = {};

    hal_mock_spi_push_rx(1, scripted_rx, sizeof(scripted_rx));
    hal_spi_transfer_txrx(1, tx, rx, sizeof(tx));

    TEST_ASSERT_EQUAL_UINT8(0xA1, rx[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA2, rx[1]);
    TEST_ASSERT_EQUAL_UINT8(0xA3, rx[2]);
    TEST_ASSERT_EQUAL_size_t(3u, hal_mock_spi_get_tx(1, tx_log, sizeof(tx_log)));
    TEST_ASSERT_EQUAL_UINT8(0x01, tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, tx_log[1]);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx_log[2]);
}

void test_spi_cpp_compat_class_uses_hal_backend(void) {
    uint8_t scripted_rx[] = {0x5A};

    SPI.setRX(6);
    SPI.setTX(7);
    SPI.setSCK(5);
    SPI.begin();
    SPI.beginTransaction(SPISettings(2000000u, LSBFIRST, SPI_MODE2));
    hal_mock_spi_push_rx(0, scripted_rx, sizeof(scripted_rx));

    TEST_ASSERT_TRUE(hal_mock_spi_is_initialized());
    TEST_ASSERT_EQUAL_UINT8(0, hal_mock_spi_get_bus());
    TEST_ASSERT_TRUE(hal_mock_spi_transaction_active(0));
    TEST_ASSERT_EQUAL_UINT32(2000000u, hal_mock_spi_get_clock_hz(0));
    TEST_ASSERT_EQUAL_UINT8(HAL_SPI_LSBFIRST, hal_mock_spi_get_bit_order(0));
    TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE2, hal_mock_spi_get_data_mode(0));
    TEST_ASSERT_EQUAL_UINT8(0x5A, SPI.transfer(0x99));

    SPI.endTransaction();
    TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_spi_not_initialized_after_reset);
    RUN_TEST(test_spi_init_stores_bus_and_pins);
    RUN_TEST(test_spi_reinit_overwrites_previous_values);
    RUN_TEST(test_spi_reset_clears_state);
    RUN_TEST(test_spi_lock_unlock_tracks_per_bus_depth);
    RUN_TEST(test_spi_transaction_settings_are_stored_per_bus);
    RUN_TEST(test_spi_transfer_records_tx_and_reads_scripted_rx);
    RUN_TEST(test_spi_transfer16_respects_active_bit_order);
    RUN_TEST(test_spi_transfer_txrx_supports_full_duplex_buffers);
    RUN_TEST(test_spi_cpp_compat_class_uses_hal_backend);
    return UNITY_END();
}

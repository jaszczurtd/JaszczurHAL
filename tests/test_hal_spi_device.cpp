#include "hal/hal_spi_device.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_gpio_trace_reset();
}

void tearDown(void) {}

void test_init_stores_effective_configuration_and_deasserts_cs(void) {
  hal_spi_device_t device = {};
  const hal_spi_settings_t settings = {0u, HAL_SPI_LSBFIRST, HAL_SPI_MODE3};

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_spi_device_init(&device, 1u, 17u, &settings));
  TEST_ASSERT_EQUAL_UINT32(HAL_SPI_CLOCK_DEFAULT_HZ, device.settings.clock_hz);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_LSBFIRST, device.settings.bit_order);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE3, device.settings.data_mode);
  TEST_ASSERT_EQUAL_UINT8(1u, device.bus);
  TEST_ASSERT_EQUAL_UINT8(17u, device.cs_pin);
  TEST_ASSERT_TRUE(device.initialized);
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT_HIGH, hal_mock_gpio_get_mode(17u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(17u));
}

void test_init_accepts_default_settings_and_no_cs(void) {
  hal_spi_device_t device = {};

  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_spi_device_init(&device, 0u, HAL_SPI_DEVICE_CS_NONE, nullptr));
  TEST_ASSERT_EQUAL_UINT32(HAL_SPI_CLOCK_DEFAULT_HZ, device.settings.clock_hz);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MSBFIRST, device.settings.bit_order);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE0, device.settings.data_mode);
  TEST_ASSERT_EQUAL_UINT(0u, hal_mock_gpio_trace_count());
}

void test_init_rejects_invalid_configuration(void) {
  hal_spi_device_t device = {};
  const hal_spi_settings_t bad_order = {1000000u, 2u, HAL_SPI_MODE0};
  const hal_spi_settings_t bad_mode = {1000000u, HAL_SPI_MSBFIRST, 4u};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_spi_device_init(nullptr, 0u, 1u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_spi_device_init(&device, 2u, 1u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_spi_device_init(&device, 0u, 1u, &bad_order));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_spi_device_init(&device, 0u, 1u, &bad_mode));
}

void test_acquire_and_release_own_bus_settings_and_cs(void) {
  hal_spi_device_t device = {};
  const hal_spi_settings_t settings = {12000000u, HAL_SPI_LSBFIRST,
                                       HAL_SPI_MODE2};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_init(1u, 2u, 3u, 4u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_spi_device_init(&device, 1u, 17u, &settings));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_acquire(&device));
  TEST_ASSERT_TRUE(device.acquired);
  TEST_ASSERT_EQUAL_INT(1, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_TRUE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(17u));
  TEST_ASSERT_EQUAL_UINT32(12000000u, hal_mock_spi_get_clock_hz(1u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_LSBFIRST, hal_mock_spi_get_bit_order(1u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE2, hal_mock_spi_get_data_mode(1u));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_release(&device));
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(17u));
}

void test_transaction_executes_all_buffer_operation_types(void) {
  hal_spi_device_t device = {};
  uint8_t read_data[2] = {};
  uint8_t transfer_rx[2] = {};
  uint8_t in_place[2] = {0x70u, 0x71u};
  const uint8_t write_data[2] = {0x10u, 0x11u};
  const uint8_t transfer_tx[2] = {0x50u, 0x51u};
  const uint8_t scripted_rx[8] = {0x20u, 0x21u, 0x30u, 0x31u,
                                  0x60u, 0x61u, 0x80u, 0x81u};
  const hal_spi_device_operation_t operations[] = {
      {HAL_SPI_DEVICE_OP_WRITE, write_data, nullptr, sizeof(write_data)},
      {HAL_SPI_DEVICE_OP_READ, nullptr, read_data, sizeof(read_data)},
      {HAL_SPI_DEVICE_OP_TRANSFER, transfer_tx, transfer_rx,
       sizeof(transfer_tx)},
      {HAL_SPI_DEVICE_OP_TRANSFER_IN_PLACE, nullptr, in_place,
       sizeof(in_place)},
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_init(0u, 2u, 3u, 4u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_init(&device, 0u, 9u, nullptr));
  hal_mock_spi_push_rx(0u, scripted_rx, sizeof(scripted_rx));

  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_spi_device_transaction(&device, operations,
                                 sizeof(operations) / sizeof(operations[0])));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(&scripted_rx[2], read_data, sizeof(read_data));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(&scripted_rx[4], transfer_rx,
                                sizeof(transfer_rx));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(&scripted_rx[6], in_place, sizeof(in_place));
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(9u));

  uint8_t tx_log[8] = {};
  TEST_ASSERT_EQUAL_UINT(8u, hal_mock_spi_get_tx(0u, tx_log, sizeof(tx_log)));
  const uint8_t expected_tx[8] = {0x10u, 0x11u, 0xFFu, 0xFFu,
                                  0x50u, 0x51u, 0x70u, 0x71u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_tx, tx_log, sizeof(expected_tx));
}

void test_lifecycle_and_operation_validation_is_status_first(void) {
  hal_spi_device_t device = {};
  const hal_spi_device_operation_t bad_operation = {HAL_SPI_DEVICE_OP_WRITE,
                                                    nullptr, nullptr, 1u};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_spi_device_acquire(&device));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_spi_device_release(&device));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_init(&device, 0u, 8u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_spi_device_release(&device));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_spi_device_transaction(&device, &bad_operation, 1u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(8u));
}

void test_begin_failure_unlocks_bus_without_asserting_cs(void) {
  hal_spi_device_t device = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_init(&device, 0u, 8u, nullptr));
  hal_mock_spi_fail_next_begin(0u, true);

  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_spi_device_acquire(&device));
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(8u));
}

void test_operation_failure_deasserts_cs_ends_and_unlocks(void) {
  hal_spi_device_t device = {};
  const uint8_t data = 0x5Au;
  const hal_spi_device_operation_t operation = {HAL_SPI_DEVICE_OP_WRITE, &data,
                                                nullptr, 1u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_init(&device, 0u, 8u, nullptr));
  hal_mock_spi_fail_next_write(0u, true);

  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        hal_spi_device_transaction(&device, &operation, 1u));
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(8u));
}

void test_end_failure_still_deasserts_cs_and_unlocks(void) {
  hal_spi_device_t device = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_init(&device, 0u, 8u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_acquire(&device));
  hal_mock_spi_fail_next_end(0u, true);

  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_spi_device_finish(&device, HAL_OK));
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(8u));
}

void test_finish_preserves_operation_error_over_end_failure(void) {
  hal_spi_device_t device = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_init(&device, 0u, 8u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_acquire(&device));
  hal_mock_spi_fail_next_end(0u, true);

  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        hal_spi_device_finish(&device, HAL_ETIMEOUT));
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(8u));
}

void test_device_can_retry_after_failed_transaction(void) {
  hal_spi_device_t device = {};
  const uint8_t data = 0xA5u;
  const hal_spi_device_operation_t operation = {HAL_SPI_DEVICE_OP_WRITE, &data,
                                                nullptr, 1u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_device_init(&device, 0u, 8u, nullptr));
  hal_mock_spi_fail_next_write(0u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        hal_spi_device_transaction(&device, &operation, 1u));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_spi_device_transaction(&device, &operation, 1u));
  TEST_ASSERT_FALSE(device.acquired);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(8u));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_init_stores_effective_configuration_and_deasserts_cs);
  RUN_TEST(test_init_accepts_default_settings_and_no_cs);
  RUN_TEST(test_init_rejects_invalid_configuration);
  RUN_TEST(test_acquire_and_release_own_bus_settings_and_cs);
  RUN_TEST(test_transaction_executes_all_buffer_operation_types);
  RUN_TEST(test_lifecycle_and_operation_validation_is_status_first);
  RUN_TEST(test_begin_failure_unlocks_bus_without_asserting_cs);
  RUN_TEST(test_operation_failure_deasserts_cs_ends_and_unlocks);
  RUN_TEST(test_end_failure_still_deasserts_cs_and_unlocks);
  RUN_TEST(test_finish_preserves_operation_error_over_end_failure);
  RUN_TEST(test_device_can_retry_after_failed_transaction);
  return UNITY_END();
}

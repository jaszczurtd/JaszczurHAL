#include "utils/unity.h"

#include "hal/hal_i2c.h"
#include "hal/hal_spi.h"
#include "hal/hal_stmpe610.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

#include <stddef.h>
#include <string.h>

#ifndef HAL_ENABLE_STMPE610
#error "HAL_ENABLE_STMPE610 must be defined for test_stmpe610_driver"
#endif

static hal_stmpe610_t dev;

static void inject_i2c_init_success(uint8_t bus) {
  uint8_t rx[67];

  memset(rx, 0, sizeof(rx));
  rx[0] = 0x08u;
  rx[1] = 0x11u;
  hal_mock_i2c_inject_rx_bus(bus, rx, sizeof(rx));
}

static void append_spi_read(uint8_t *rx, size_t *len, uint8_t value) {
  rx[(*len)++] = 0u;
  rx[(*len)++] = 0u;
  rx[(*len)++] = value;
}

static void append_soft_read(bool *levels, size_t *len, uint8_t value) {
  for (uint8_t mask = 0x80u; mask != 0u; mask = (uint8_t)(mask >> 1u)) {
    levels[(*len)++] = (value & mask) != 0u;
  }
}

static void inject_spi_init_success(uint8_t bus) {
  uint8_t rx[201];
  size_t len = 0u;

  append_spi_read(rx, &len, 0x08u);
  append_spi_read(rx, &len, 0x11u);
  for (uint8_t i = 0u; i < 65u; ++i) {
    append_spi_read(rx, &len, 0u);
  }

  hal_mock_spi_push_rx(bus, rx, len);
}

static void inject_soft_spi_init_success(uint8_t miso_pin) {
  bool levels[536];
  size_t len = 0u;

  append_soft_read(levels, &len, 0x08u);
  append_soft_read(levels, &len, 0x11u);
  for (uint8_t i = 0u; i < 65u; ++i) {
    append_soft_read(levels, &len, 0u);
  }

  hal_mock_gpio_push_read_sequence(miso_pin, levels, len);
}

static bool find_i2c_frame(uint8_t bus, uint8_t a, uint8_t b) {
  const int count = hal_mock_i2c_get_write_frame_count_bus(bus);

  for (int i = 0; i < count; ++i) {
    uint8_t frame[4] = {0u, 0u, 0u, 0u};
    const int len =
        hal_mock_i2c_get_write_frame_bus(bus, i, frame, (int)sizeof(frame));
    if ((len == 2u) && (frame[0] == a) && (frame[1] == b)) {
      return true;
    }
  }

  return false;
}

static bool find_i2c_reg_frame(uint8_t bus, uint8_t reg) {
  const int count = hal_mock_i2c_get_write_frame_count_bus(bus);

  for (int i = 0; i < count; ++i) {
    uint8_t frame[4] = {0u, 0u, 0u, 0u};
    const int len =
        hal_mock_i2c_get_write_frame_bus(bus, i, frame, (int)sizeof(frame));
    if ((len == 1) && (frame[0] == reg)) {
      return true;
    }
  }

  return false;
}

void setUp(void) {
  memset(&dev, 0, sizeof(dev));
  hal_mock_spi_reset();
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
  hal_mock_set_millis(0u);
  hal_i2c_init_bus(0u, 4u, 5u, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_i2c_init_bus(1u, 6u, 7u, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_spi_init(0u, 16u, 19u, 18u);
  hal_spi_init(1u, 20u, 23u, 22u);
}

void tearDown(void) { hal_stmpe610_deinit(&dev); }

void test_default_config_uses_i2c_address(void) {
  const hal_stmpe610_config_t cfg = hal_stmpe610_default_config();

  TEST_ASSERT_EQUAL(HAL_STMPE610_TRANSPORT_I2C, cfg.transport);
  TEST_ASSERT_EQUAL_UINT8(0u, cfg.i2c_bus);
  TEST_ASSERT_EQUAL_UINT8(HAL_STMPE610_I2C_ADDR_DEFAULT, cfg.i2c_addr);
  TEST_ASSERT_EQUAL_UINT8(HAL_STMPE610_PIN_NONE, cfg.cs_pin);
}

void test_i2c_init_runs_reference_setup_sequence(void) {
  inject_i2c_init_success(0u);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_stmpe610_init_ex(&dev, NULL));
  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_NOT_NULL(dev.mutex);
  TEST_ASSERT_EQUAL_UINT32(10u, hal_millis());
}

void test_i2c_init_rejects_wrong_chip_id(void) {
  const uint8_t rx[] = {0x00u, 0x00u};

  hal_mock_i2c_inject_rx_bus(0u, rx, sizeof(rx));

  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_stmpe610_init_ex(&dev, NULL));
  TEST_ASSERT_FALSE(dev.initialized);
  TEST_ASSERT_NULL(dev.mutex);
}

void test_init_ex_rejects_invalid_arguments_and_config(void) {
  hal_stmpe610_config_t bad =
      hal_stmpe610_spi_config(0u, HAL_STMPE610_PIN_NONE);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_stmpe610_init_ex(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_stmpe610_init_ex(&dev, &bad));
}

void test_i2c_register_access_uses_expected_frames(void) {
  uint8_t read_value = 0x5Au;

  inject_i2c_init_success(0u);
  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, NULL));

  hal_mock_i2c_reset_write_log_bus(0u);
  hal_mock_i2c_inject_rx_bus(0u, &read_value, 1u);
  TEST_ASSERT_EQUAL_UINT8(
      0x5Au, hal_stmpe610_read_register8(&dev, HAL_STMPE610_REG_TSC_CTRL));
  TEST_ASSERT_TRUE(find_i2c_reg_frame(0u, HAL_STMPE610_REG_TSC_CTRL));

  hal_stmpe610_write_register8(&dev, HAL_STMPE610_REG_INT_STA, 0xFFu);
  TEST_ASSERT_TRUE(find_i2c_frame(0u, HAL_STMPE610_REG_INT_STA, 0xFFu));
}

void test_i2c_read16_reads_two_adjacent_registers(void) {
  const uint8_t rx[] = {0x12u, 0x34u};
  uint8_t frame[2] = {0u, 0u};

  inject_i2c_init_success(0u);
  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, NULL));

  hal_mock_i2c_reset_write_log_bus(0u);
  hal_mock_i2c_inject_rx_bus(0u, rx, sizeof(rx));
  TEST_ASSERT_EQUAL_UINT16(
      0x1234u, hal_stmpe610_read_register16(&dev, HAL_STMPE610_REG_TSC_DATA_X));

  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count_bus(0u));
  TEST_ASSERT_EQUAL_INT(
      1, hal_mock_i2c_get_write_frame_bus(0u, 0, frame, (int)sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(HAL_STMPE610_REG_TSC_DATA_X, frame[0]);
  TEST_ASSERT_EQUAL_INT(
      1, hal_mock_i2c_get_write_frame_bus(0u, 1, frame, (int)sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(HAL_STMPE610_REG_TSC_DATA_X + 1u),
                          frame[0]);
}

void test_read_data_decodes_fifo_bytes(void) {
  uint16_t x = 0u;
  uint16_t y = 0u;
  uint8_t z = 0u;
  const uint8_t rx[] = {0x12u, 0x34u, 0x56u, 0x78u};

  inject_i2c_init_success(0u);
  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, NULL));

  hal_mock_i2c_reset_write_log_bus(0u);
  hal_mock_i2c_inject_rx_bus(0u, rx, sizeof(rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_stmpe610_read_data_ex(&dev, &x, &y, &z));

  TEST_ASSERT_EQUAL_UINT16(0x0123u, x);
  TEST_ASSERT_EQUAL_UINT16(0x0456u, y);
  TEST_ASSERT_EQUAL_UINT8(0x78u, z);
}

void test_read_data_ex_reports_invalid_and_uninitialized(void) {
  uint16_t x = 9u;
  uint16_t y = 8u;
  uint8_t z = 7u;

  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_stmpe610_read_data_ex(&dev, &x, &y, &z));
  TEST_ASSERT_EQUAL_UINT16(0u, x);
  TEST_ASSERT_EQUAL_UINT16(0u, y);
  TEST_ASSERT_EQUAL_UINT8(0u, z);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_stmpe610_read_data_ex(&dev, NULL, &y, &z));
}

void test_get_point_drains_fifo_and_clears_interrupts(void) {
  const uint8_t rx[] = {0x00u,
                        0x12u,
                        0x34u,
                        0x56u,
                        0x78u,
                        HAL_STMPE610_FIFO_STA_EMPTY,
                        HAL_STMPE610_FIFO_STA_EMPTY};

  inject_i2c_init_success(0u);
  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, NULL));

  hal_mock_i2c_reset_write_log_bus(0u);
  hal_mock_i2c_inject_rx_bus(0u, rx, sizeof(rx));

  const hal_stmpe610_point_t point = hal_stmpe610_get_point(&dev);

  TEST_ASSERT_EQUAL_INT16(0x0123, point.x);
  TEST_ASSERT_EQUAL_INT16(0x0456, point.y);
  TEST_ASSERT_EQUAL_INT16(0x0078, point.z);
  TEST_ASSERT_TRUE(find_i2c_frame(0u, HAL_STMPE610_REG_INT_STA, 0xFFu));
}

void test_get_point_locks_driver_once(void) {
  const uint8_t rx[] = {HAL_STMPE610_FIFO_STA_EMPTY,
                        HAL_STMPE610_FIFO_STA_EMPTY};

  inject_i2c_init_success(0u);
  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, NULL));

  hal_mock_mutex_stats_reset();
  hal_mock_i2c_reset_write_log_bus(0u);
  hal_mock_i2c_inject_rx_bus(0u, rx, sizeof(rx));
  (void)hal_stmpe610_get_point(&dev);

  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_lock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_unlock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_max_depth());
}

void test_spi_init_uses_mode0_and_chip_select(void) {
  const hal_stmpe610_config_t cfg = hal_stmpe610_spi_config(0u, 10u);

  inject_spi_init_success(0u);

  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, &cfg));
  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE0, dev.spi_mode);
  TEST_ASSERT_EQUAL_UINT32(HAL_STMPE610_SPI_CLOCK_HZ,
                           hal_mock_spi_get_clock_hz(0u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE0, hal_mock_spi_get_data_mode(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));
}

void test_spi_init_falls_back_to_mode1_on_version_probe(void) {
  const hal_stmpe610_config_t cfg = hal_stmpe610_spi_config(0u, 10u);
  uint8_t rx[207];
  size_t len = 0u;

  append_spi_read(rx, &len, 0x00u);
  append_spi_read(rx, &len, 0x00u);
  append_spi_read(rx, &len, 0x08u);
  append_spi_read(rx, &len, 0x11u);
  for (uint8_t i = 0u; i < 65u; ++i) {
    append_spi_read(rx, &len, 0u);
  }

  hal_mock_spi_push_rx(0u, rx, len);

  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, &cfg));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE1, dev.spi_mode);
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE1, hal_mock_spi_get_data_mode(0u));
}

void test_spi_register_access_transfers_reference_sequence(void) {
  const hal_stmpe610_config_t cfg = hal_stmpe610_spi_config(0u, 10u);
  const uint8_t rx[] = {0u, 0u, 0xABu};
  uint8_t tx[8];

  inject_spi_init_success(0u);
  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, &cfg));

  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx, sizeof(rx));
  TEST_ASSERT_EQUAL_UINT8(
      0xABu, hal_stmpe610_read_register8(&dev, HAL_STMPE610_REG_TSC_CTRL));

  TEST_ASSERT_EQUAL_size_t(3u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x80u | HAL_STMPE610_REG_TSC_CTRL), tx[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, tx[1]);
  TEST_ASSERT_EQUAL_UINT8(0u, tx[2]);
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_spi_get_lock_depth(0u));

  hal_mock_spi_reset();
  hal_stmpe610_write_register8(&dev, HAL_STMPE610_REG_INT_STA, 0xFFu);
  TEST_ASSERT_EQUAL_size_t(2u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
  TEST_ASSERT_EQUAL_UINT8(HAL_STMPE610_REG_INT_STA, tx[0]);
  TEST_ASSERT_EQUAL_UINT8(0xFFu, tx[1]);
}

void test_spi_read16_uses_two_independent_read_frames(void) {
  const hal_stmpe610_config_t cfg = hal_stmpe610_spi_config(0u, 10u);
  uint8_t rx[6];
  uint8_t tx[8];
  size_t len = 0u;

  inject_spi_init_success(0u);
  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, &cfg));

  append_spi_read(rx, &len, 0x12u);
  append_spi_read(rx, &len, 0x34u);
  hal_mock_spi_reset();
  hal_mock_spi_push_rx(0u, rx, len);

  TEST_ASSERT_EQUAL_UINT16(
      0x1234u, hal_stmpe610_read_register16(&dev, HAL_STMPE610_REG_TSC_DATA_X));

  TEST_ASSERT_EQUAL_size_t(6u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x80u | HAL_STMPE610_REG_TSC_DATA_X),
                          tx[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, tx[1]);
  TEST_ASSERT_EQUAL_UINT8(0u, tx[2]);
  TEST_ASSERT_EQUAL_UINT8(
      (uint8_t)(0x80u | (uint8_t)(HAL_STMPE610_REG_TSC_DATA_X + 1u)), tx[3]);
  TEST_ASSERT_EQUAL_UINT8(0u, tx[4]);
  TEST_ASSERT_EQUAL_UINT8(0u, tx[5]);
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_spi_get_lock_depth(0u));
}

void test_soft_spi_init_uses_gpio_bitbang_path(void) {
  const hal_stmpe610_config_t cfg =
      hal_stmpe610_soft_spi_config(10u, 11u, 12u, 13u);

  inject_soft_spi_init_success(12u);

  TEST_ASSERT_TRUE(hal_stmpe610_init(&dev, &cfg));
  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));
  TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(10u));
  TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(11u));
  TEST_ASSERT_EQUAL(HAL_GPIO_INPUT, hal_mock_gpio_get_mode(12u));
  TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(13u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_spi_get_transfer_count(0u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_uses_i2c_address);
  RUN_TEST(test_i2c_init_runs_reference_setup_sequence);
  RUN_TEST(test_i2c_init_rejects_wrong_chip_id);
  RUN_TEST(test_init_ex_rejects_invalid_arguments_and_config);
  RUN_TEST(test_i2c_register_access_uses_expected_frames);
  RUN_TEST(test_i2c_read16_reads_two_adjacent_registers);
  RUN_TEST(test_read_data_decodes_fifo_bytes);
  RUN_TEST(test_read_data_ex_reports_invalid_and_uninitialized);
  RUN_TEST(test_get_point_drains_fifo_and_clears_interrupts);
  RUN_TEST(test_get_point_locks_driver_once);
  RUN_TEST(test_spi_init_uses_mode0_and_chip_select);
  RUN_TEST(test_spi_init_falls_back_to_mode1_on_version_probe);
  RUN_TEST(test_spi_register_access_transfers_reference_sequence);
  RUN_TEST(test_spi_read16_uses_two_independent_read_frames);
  RUN_TEST(test_soft_spi_init_uses_gpio_bitbang_path);
  return UNITY_END();
}

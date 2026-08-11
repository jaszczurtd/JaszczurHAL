#include "hal/analog/hal_mcp3221.h"
#include "hal/analog/hal_mcp4725.h"
#include "hal/gpio/hal_hc595.h"
#include "hal/gpio/hal_mcp23017.h"
#include "hal/gpio/hal_pca9654e.h"
#include "hal/gpio/hal_pcf8574.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/spi/hal_spi.h"
#include "utils/unity.h"

static uint8_t frame[8];

void setUp(void) {
  hal_i2c_init_bus(0, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_i2c_init_bus(1, 6, 7, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_set_busy_bus(1, false);
  hal_mock_i2c_reset_write_log();
  hal_mock_i2c_reset_write_log_bus(1);
  hal_spi_init(0, 16, 19, 18);
  hal_mock_spi_reset();
  hal_mock_gpio_trace_reset();
}

void tearDown(void) {}

static int get_i2c_frame(int index) {
  int n = hal_mock_i2c_get_write_frame(index, frame, sizeof(frame));
  TEST_ASSERT_TRUE(n >= 0);
  return n;
}

void test_pca9654e_init_and_inverted_output_follow_source_registers(void) {
  hal_pca9654e_t dev = {};

  TEST_ASSERT_EQUAL(HAL_OK, hal_pca9654e_init_ex(&dev, NULL));
  TEST_ASSERT_EQUAL_INT(3, hal_mock_i2c_get_write_frame_count());

  TEST_ASSERT_EQUAL_INT(2, get_i2c_frame(0));
  TEST_ASSERT_EQUAL_UINT8(3u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, frame[1]);
  TEST_ASSERT_EQUAL_INT(2, get_i2c_frame(1));
  TEST_ASSERT_EQUAL_UINT8(2u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, frame[1]);
  TEST_ASSERT_EQUAL_INT(2, get_i2c_frame(2));
  TEST_ASSERT_EQUAL_UINT8(1u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, frame[1]);

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_EQUAL(HAL_OK, hal_pca9654e_config_pin_ex(&dev, 2u, true));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pca9654e_write_pin_ex(&dev, 2u, true));
  TEST_ASSERT_EQUAL_UINT8(0u, hal_pca9654e_output_latch(&dev));

  TEST_ASSERT_EQUAL_INT(2, get_i2c_frame(1));
  TEST_ASSERT_EQUAL_UINT8(1u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, frame[1]);

  hal_pca9654e_deinit(&dev);
}

void test_pcf8574_quasi_bidirectional_latch_write_and_read(void) {
  hal_pcf8574_t dev = {};
  hal_pcf8574_config_t cfg = hal_pcf8574_default_config();
  cfg.initial_latch = 0xFFu;

  TEST_ASSERT_EQUAL(HAL_OK, hal_pcf8574_init_ex(&dev, &cfg));
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(1, get_i2c_frame(0));
  TEST_ASSERT_EQUAL_UINT8(0xFFu, frame[0]);

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_EQUAL(HAL_OK, hal_pcf8574_config_pin_ex(&dev, 1u, true));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pcf8574_write_pin_ex(&dev, 1u, true));
  TEST_ASSERT_EQUAL_UINT8(0xFDu, hal_pcf8574_output_latch(&dev));
  TEST_ASSERT_EQUAL_INT(1, get_i2c_frame(1));
  TEST_ASSERT_EQUAL_UINT8(0xFDu, frame[0]);

  const uint8_t rx[] = {0xF9u};
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
  uint8_t value = 0u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_pcf8574_read_all_ex(&dev, &value));
  TEST_ASSERT_EQUAL_UINT8(0xFBu, value);

  hal_pcf8574_deinit(&dev);
}

void test_mcp23017_split_mode_init_and_output_write(void) {
  hal_mcp23017_t dev = {};

  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp23017_init_ex(&dev, NULL));
  TEST_ASSERT_EQUAL_UINT8(8u, hal_mcp23017_input_count(&dev));
  TEST_ASSERT_EQUAL_UINT8(8u, hal_mcp23017_output_count(&dev));

  TEST_ASSERT_EQUAL_INT(2, get_i2c_frame(0));
  TEST_ASSERT_EQUAL_UINT8(0x00u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, frame[1]);
  TEST_ASSERT_EQUAL_INT(2, get_i2c_frame(1));
  TEST_ASSERT_EQUAL_UINT8(0x01u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0xFFu, frame[1]);

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp23017_write_pin_ex(&dev, 3u, true));
  TEST_ASSERT_EQUAL_UINT16(0x0008u, hal_mcp23017_output_latch(&dev));
  TEST_ASSERT_EQUAL_INT(2, get_i2c_frame(0));
  TEST_ASSERT_EQUAL_UINT8(0x12u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0x08u, frame[1]);

  hal_mcp23017_deinit(&dev);
}

void test_mcp23017_16_input_config_uses_port_a_register_pair(void) {
  hal_mcp23017_t dev = {};
  hal_mcp23017_config_t cfg = hal_mcp23017_default_config();
  cfg.mode = HAL_MCP23017_MODE_16_IN;

  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp23017_init_ex(&dev, &cfg));
  hal_mock_i2c_reset_write_log();

  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp23017_config_input_ex(&dev, 9u, true, true));
  TEST_ASSERT_EQUAL_INT(3, get_i2c_frame(0));
  TEST_ASSERT_EQUAL_UINT8(0x02u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02u, frame[2]);
  TEST_ASSERT_EQUAL_INT(3, get_i2c_frame(1));
  TEST_ASSERT_EQUAL_UINT8(0x0Cu, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02u, frame[2]);

  const uint8_t rx[] = {0x34u, 0x12u};
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
  uint16_t value = 0u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp23017_read_all_ex(&dev, &value));
  TEST_ASSERT_EQUAL_UINT16(0x1234u, value);

  hal_mcp23017_deinit(&dev);
}

void test_hc595_writes_highest_shift_register_byte_first_and_toggles_cs(void) {
  hal_hc595_t dev = {};
  hal_hc595_config_t cfg = hal_hc595_default_config(10u);
  cfg.chips = 3u;
  cfg.clock_hz = 4000000u;

  TEST_ASSERT_EQUAL(HAL_OK, hal_hc595_init_ex(&dev, &cfg));
  hal_mock_spi_reset();
  hal_mock_gpio_trace_reset();

  TEST_ASSERT_EQUAL(HAL_OK, hal_hc595_write_all_ex(&dev, 0x00A1B2C3u));

  uint8_t tx[4] = {};
  TEST_ASSERT_EQUAL_UINT(3u, hal_mock_spi_get_tx(0, tx, sizeof(tx)));
  TEST_ASSERT_EQUAL_UINT8(0xA1u, tx[0]);
  TEST_ASSERT_EQUAL_UINT8(0xB2u, tx[1]);
  TEST_ASSERT_EQUAL_UINT8(0xC3u, tx[2]);
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));

  hal_hc595_deinit(&dev);
}

void test_mcp3221_reads_two_byte_sample_without_masking_source_behavior(void) {
  hal_mcp3221_t dev = {};
  const uint8_t rx[] = {0x1Au, 0xBCu};

  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp3221_init_ex(&dev, NULL));
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));

  uint16_t value = 0u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp3221_read_ex(&dev, &value));
  TEST_ASSERT_EQUAL_UINT16(0x1ABCu, value);

  hal_mcp3221_deinit(&dev);
}

void test_mcp4725_init_reads_eeprom_value_and_write_encodes_fast_mode(void) {
  hal_mcp4725_t dev = {};
  const uint8_t rx[] = {0x00u, 0x12u, 0x30u, 0x00u, 0x00u};

  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp4725_init_ex(&dev, NULL));
  TEST_ASSERT_EQUAL_UINT16(0x0123u, hal_mcp4725_output_latch(&dev));

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_EQUAL(HAL_OK, hal_mcp4725_write_ex(&dev, 0x0ABCu));
  TEST_ASSERT_EQUAL_UINT16(0x0ABCu, hal_mcp4725_output_latch(&dev));
  TEST_ASSERT_EQUAL_INT(3, get_i2c_frame(0));
  TEST_ASSERT_EQUAL_UINT8(0x40u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0xABu, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(0xC0u, frame[2]);

  hal_mcp4725_deinit(&dev);
}

void test_status_errors_are_reported_for_invalid_or_busy_bus(void) {
  hal_mcp4725_t dac = {};
  hal_mock_i2c_set_busy(true);
  TEST_ASSERT_EQUAL(HAL_EBUS, hal_mcp4725_init_ex(&dac, NULL));

  hal_mcp23017_t expander = {};
  TEST_ASSERT_EQUAL(HAL_EUNINIT,
                    hal_mcp23017_write_pin_ex(&expander, 0u, true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pca9654e_init_and_inverted_output_follow_source_registers);
  RUN_TEST(test_pcf8574_quasi_bidirectional_latch_write_and_read);
  RUN_TEST(test_mcp23017_split_mode_init_and_output_write);
  RUN_TEST(test_mcp23017_16_input_config_uses_port_a_register_pair);
  RUN_TEST(test_hc595_writes_highest_shift_register_byte_first_and_toggles_cs);
  RUN_TEST(test_mcp3221_reads_two_byte_sample_without_masking_source_behavior);
  RUN_TEST(test_mcp4725_init_reads_eeprom_value_and_write_encodes_fast_mode);
  RUN_TEST(test_status_errors_are_reported_for_invalid_or_busy_bus);
  return UNITY_END();
}

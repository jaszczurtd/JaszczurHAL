#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/sensors/hal_bh1750.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

static hal_bh1750_t dev;

void setUp(void) {
  dev = {};
  hal_i2c_init_bus(0, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_i2c_init_bus(1, 6, 7, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_set_busy_bus(1, false);
  hal_mock_i2c_reset_write_log();
  hal_mock_i2c_reset_write_log_bus(1);
  hal_mock_set_millis(0u);
}

void tearDown(void) { hal_bh1750_deinit(&dev); }

static int get_frame(uint8_t *buf, int max) {
  int n = hal_mock_i2c_get_write_frame(0, buf, max);
  TEST_ASSERT_TRUE_MESSAGE(n >= 0, "expected BH1750 write frame");
  return n;
}

void test_default_config_preserves_source_driver_default_address(void) {
  hal_bh1750_config_t cfg = hal_bh1750_default_config();

  TEST_ASSERT_EQUAL_UINT8(0u, cfg.i2c_bus);
  TEST_ASSERT_EQUAL_UINT8(HAL_BH1750_I2C_ADDR_HIGH, cfg.i2c_addr);
}

void test_init_sends_continuous_h_resolution_command_and_waits(void) {
  hal_bh1750_config_t cfg = hal_bh1750_default_config();
  cfg.i2c_addr = HAL_BH1750_I2C_ADDR_LOW;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bh1750_init_ex(&dev, &cfg));
  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_EQUAL_UINT32(180u, hal_millis());
  TEST_ASSERT_EQUAL_UINT8(HAL_BH1750_I2C_ADDR_LOW,
                          hal_mock_i2c_get_last_addr());

  uint8_t frame[2] = {};
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(1, get_frame(frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x10u, frame[0]);
}

void test_init_failure_does_not_wait_and_releases_driver(void) {
  hal_mock_i2c_set_busy(true);

  TEST_ASSERT_EQUAL_INT(HAL_EBUS, hal_bh1750_init_ex(&dev, NULL));
  TEST_ASSERT_FALSE(hal_bh1750_init(&dev, NULL));
  TEST_ASSERT_FALSE(dev.initialized);
  TEST_ASSERT_NULL(dev.mutex);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_millis());
}

void test_init_ex_rejects_null_driver(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_bh1750_init_ex(NULL, NULL));
}

void test_light_decodes_two_byte_sample_as_lux(void) {
  hal_bh1750_config_t cfg = hal_bh1750_default_config();
  cfg.i2c_addr = HAL_BH1750_I2C_ADDR_LOW;
  TEST_ASSERT_TRUE(hal_bh1750_init(&dev, &cfg));

  const uint8_t rx[] = {0x01u, 0xE0u}; /* 480 / 1.2 = 400 lx */
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
  uint32_t before = hal_i2c_get_transaction_count();

  float lux = -1.0f;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bh1750_light_ex(&dev, &lux));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 400.0f, lux);
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 400.0f, hal_bh1750_light(&dev));
  TEST_ASSERT_EQUAL_UINT32(before + 2u, hal_i2c_get_transaction_count());
}

void test_light_routes_to_selected_i2c_bus(void) {
  hal_bh1750_config_t cfg = hal_bh1750_default_config();
  cfg.i2c_bus = 1u;
  cfg.i2c_addr = HAL_BH1750_I2C_ADDR_HIGH;
  TEST_ASSERT_TRUE(hal_bh1750_init(&dev, &cfg));

  const uint8_t rx0[] = {0x00u, 0x00u};
  const uint8_t rx1[] = {0x02u, 0x58u}; /* 600 / 1.2 = 500 lx */
  hal_mock_i2c_inject_rx(rx0, (int)sizeof(rx0));
  hal_mock_i2c_inject_rx_bus(1, rx1, (int)sizeof(rx1));

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, hal_bh1750_light(&dev));
}

void test_light_returns_minus_one_for_invalid_driver(void) {
  float lux = 123.0f;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_bh1750_light_ex(NULL, &lux));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, lux);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_bh1750_light_ex(&dev, NULL));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, hal_bh1750_light(NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_preserves_source_driver_default_address);
  RUN_TEST(test_init_sends_continuous_h_resolution_command_and_waits);
  RUN_TEST(test_init_failure_does_not_wait_and_releases_driver);
  RUN_TEST(test_init_ex_rejects_null_driver);
  RUN_TEST(test_light_decodes_two_byte_sample_as_lux);
  RUN_TEST(test_light_routes_to_selected_i2c_bus);
  RUN_TEST(test_light_returns_minus_one_for_invalid_driver);
  return UNITY_END();
}

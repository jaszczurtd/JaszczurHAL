#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/power/hal_adp5360.h"
#include "utils/unity.h"

static hal_adp5360_t dev;

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

void tearDown(void) { hal_adp5360_deinit(&dev); }

static void make_ready(uint8_t bus = 0u) {
  dev.cfg = hal_adp5360_default_config();
  dev.cfg.i2c_bus = bus;
  dev.initialized = true;
}

static int frame_at(int index, uint8_t *buf, int max) {
  const int n = hal_mock_i2c_get_write_frame(index, buf, max);
  TEST_ASSERT_TRUE_MESSAGE(n >= 0, "expected ADP5360 write frame");
  return n;
}

static int last_frame(uint8_t *buf, int max) {
  return frame_at(hal_mock_i2c_get_write_frame_count() - 1, buf, max);
}

void test_default_config_enables_shared_pmic_sections(void) {
  hal_adp5360_config_t cfg = hal_adp5360_default_config();

  TEST_ASSERT_EQUAL_UINT8(0u, cfg.i2c_bus);
  TEST_ASSERT_EQUAL_UINT8(HAL_ADP5360_I2C_ADDR_DEFAULT, cfg.i2c_addr);
  TEST_ASSERT_TRUE(cfg.init_charger);
  TEST_ASSERT_TRUE(cfg.init_fuel_gauge);
  TEST_ASSERT_TRUE(cfg.init_buck);
  TEST_ASSERT_TRUE(cfg.init_buckboost);
  TEST_ASSERT_TRUE(cfg.enable_vout1_reset);
}

void test_minimal_init_probes_id_writes_supervisory_and_clears_interrupts(
    void) {
  hal_adp5360_config_t cfg = hal_adp5360_default_config();
  cfg.init_charger = false;
  cfg.init_fuel_gauge = false;
  cfg.init_buck = false;
  cfg.init_buckboost = false;
  cfg.i2c_addr = 0x47u;

  const uint8_t rx[] = {HAL_ADP5360_DEVICE_ID, 0xAAu, 0x55u};
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adp5360_init_ex(&dev, &cfg));
  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_EQUAL_UINT32(200u, hal_millis());
  TEST_ASSERT_EQUAL_UINT8(0x47u, hal_mock_i2c_get_last_addr());

  uint8_t frame[8] = {};
  TEST_ASSERT_TRUE(hal_mock_i2c_get_write_frame_count() >= 4);
  TEST_ASSERT_EQUAL_INT(1, frame_at(0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x00u, frame[0]);
  TEST_ASSERT_EQUAL_INT(2, frame_at(1, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x2Du, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0x80u, frame[1]);
  TEST_ASSERT_EQUAL_INT(1, frame_at(2, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x34u, frame[0]);
  TEST_ASSERT_EQUAL_INT(1, frame_at(3, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x35u, frame[0]);
}

void test_init_rejects_wrong_device_id(void) {
  const uint8_t rx[] = {0x00u};
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));

  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, hal_adp5360_init_ex(&dev, NULL));
  TEST_ASSERT_FALSE(dev.initialized);
}

void test_register_update_uses_read_modify_write_and_selected_bus(void) {
  make_ready(1u);
  const uint8_t rx[] = {0xA0u};
  hal_mock_i2c_inject_rx_bus(1, rx, (int)sizeof(rx));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_adp5360_reg_update(&dev, 0x10u, 0x0Fu, 0x05u));
  TEST_ASSERT_EQUAL_UINT8(HAL_ADP5360_I2C_ADDR_DEFAULT,
                          hal_mock_i2c_get_last_addr_bus(1));

  uint8_t frame[4] = {};
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count_bus(1));
  TEST_ASSERT_EQUAL_INT(
      1, hal_mock_i2c_get_write_frame_bus(1, 0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x10u, frame[0]);
  TEST_ASSERT_EQUAL_INT(
      2, hal_mock_i2c_get_write_frame_bus(1, 1, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x10u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0xA5u, frame[1]);
}

void test_charger_current_setters_clamp_and_encode_like_zephyr_ranges(void) {
  make_ready();
  uint8_t rx[] = {0x00u, 0x00u, 0x00u};
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_adp5360_charger_set_fast_current(&dev, 999999u));
  TEST_ASSERT_EQUAL_UINT32(320000u, dev.charger_i_fast_ua);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_adp5360_charger_set_trickle_current(&dev, 1u));
  TEST_ASSERT_EQUAL_UINT32(1000u, dev.charger_i_trickle_ua);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_adp5360_charger_set_input_current_limit(&dev, 500000u));
  TEST_ASSERT_EQUAL_UINT32(500000u, dev.charger_i_input_limit_ua);

  uint8_t frame[4] = {};
  TEST_ASSERT_EQUAL_INT(2, last_frame(frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x02u, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0x07u, frame[1]);
}

void test_fuel_gauge_decodes_voltage_soc_and_alarm(void) {
  make_ready();
  const uint8_t rx[] = {
      0x12u, 0x38u, /* VBAT: 0x1238 >> 3 = 583 mV */
      123u,         /* SOC clamps to 100 */
      0x40u,        /* alarm field 1 -> 11% */
  };
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));

  uint32_t uv = 0u;
  uint8_t pct = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_adp5360_fuel_gauge_get_voltage_uv(&dev, &uv));
  TEST_ASSERT_EQUAL_UINT32(583000u, uv);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adp5360_fuel_gauge_get_soc_pct(&dev, &pct));
  TEST_ASSERT_EQUAL_UINT8(100u, pct);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_adp5360_fuel_gauge_get_soc_alarm_pct(&dev, &pct));
  TEST_ASSERT_EQUAL_UINT8(11u, pct);
}

void test_regulator_ranges_and_voltage_programming(void) {
  make_ready();
  TEST_ASSERT_EQUAL_UINT16(
      64u, hal_adp5360_regulator_count_voltages(HAL_ADP5360_REGULATOR_BUCK));
  TEST_ASSERT_EQUAL_UINT16(64u, hal_adp5360_regulator_count_voltages(
                                    HAL_ADP5360_REGULATOR_BUCKBOOST));

  int32_t uv = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adp5360_regulator_list_voltage(
                                    HAL_ADP5360_REGULATOR_BUCKBOOST, 12u, &uv));
  TEST_ASSERT_EQUAL_INT32(2950000, uv);

  const uint8_t rx[] = {0x00u};
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_adp5360_regulator_set_voltage(
                  &dev, HAL_ADP5360_REGULATOR_BUCK, 1200000, 1200000));
  uint8_t frame[4] = {};
  TEST_ASSERT_EQUAL_INT(2, last_frame(frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x2Au, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(12u, frame[1]);
}

void test_uninitialized_and_invalid_arguments_return_status_codes(void) {
  uint8_t v = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adp5360_reg_read(NULL, 0, &v));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_adp5360_reg_read(&dev, 0, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_adp5360_reg_read(&dev, 0, &v));
  make_ready();
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_adp5360_fuel_gauge_set_soc_alarm_pct(&dev, 7u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_enables_shared_pmic_sections);
  RUN_TEST(
      test_minimal_init_probes_id_writes_supervisory_and_clears_interrupts);
  RUN_TEST(test_init_rejects_wrong_device_id);
  RUN_TEST(test_register_update_uses_read_modify_write_and_selected_bus);
  RUN_TEST(test_charger_current_setters_clamp_and_encode_like_zephyr_ranges);
  RUN_TEST(test_fuel_gauge_decodes_voltage_soc_and_alarm);
  RUN_TEST(test_regulator_ranges_and_voltage_programming);
  RUN_TEST(test_uninitialized_and_invalid_arguments_return_status_codes);
  return UNITY_END();
}

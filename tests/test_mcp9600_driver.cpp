#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/drivers/mcp9600/mcp9600_driver.h"
#include "utils/unity.h"

#include <math.h>

/* Datasheet anchors used by these tests (MCP9600/L00):
 * - Register pointer map: TH=0x00, TC=0x02, STATUS=0x04, SENSOR CFG=0x05,
 *   DEVICE CFG=0x06, ALERT CFGn=0x08..0x0B, TALERTn=0x10..0x13, DEVICE ID=0x20
 * - Temperature registers use two's complement with 0.0625 C/LSB
 * - DEVICE CFG bit7: cold-junction resolution (0=0.0625 C, 1=0.25 C)
 * - DEVICE CFG bits6:5: ADC resolution selector
 * - SENSOR CFG bits6:4 thermocouple type, bits2:0 filter coefficient
 * - ALERT CFG bits4..0 map TH/TC, rise/fall, active level, mode, enable
 */

static hal_mcp9600_t dev;

void setUp(void) {
  hal_i2c_init_bus(0, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_reset_write_log();
}

void tearDown(void) { hal_mcp9600_deinit(&dev); }

static bool init_with_id(uint8_t id) {
  const uint8_t rx[] = {id, 0x00u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mcp9600_config_t cfg = {0u, HAL_MCP9600_I2C_ADDR_DEFAULT};
  return hal_mcp9600_init(&dev, &cfg);
}

static int get_frame(int idx, uint8_t *buf, int max) {
  int n = hal_mock_i2c_get_write_frame(idx, buf, max);
  TEST_ASSERT_TRUE_MESSAGE(n >= 0, "expected write frame is missing");
  return n;
}

void test_init_accepts_mcp9600_and_writes_reset_config(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  TEST_ASSERT_EQUAL_UINT8(0x40u, hal_mcp9600_device_id(&dev));

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x20u, f[0]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x80u, f[1]);
}

void test_init_accepts_mcp9601_device_id(void) {
  TEST_ASSERT_TRUE(init_with_id(0x41u));
  TEST_ASSERT_EQUAL_UINT8(0x41u, hal_mcp9600_device_id(&dev));
}

void test_init_rejects_unknown_device_id(void) {
  TEST_ASSERT_FALSE(init_with_id(0x55u));
}

void test_read_thermocouple_decodes_signed_fixed_point(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x00u, 0x01u, 0x90u}; /* active, 25.0 C */
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, hal_mcp9600_read_thermocouple(&dev));

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
  TEST_ASSERT_EQUAL_INT(1, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x00u, f[0]);
}

void test_read_thermocouple_decodes_negative_twos_complement(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x00u, 0xFFu, 0x60u}; /* active, -10.0 C */
  hal_mock_i2c_inject_rx(rx, sizeof(rx));

  TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, hal_mcp9600_read_thermocouple(&dev));
}

void test_read_thermocouple_returns_nan_when_sleeping(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x01u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  uint32_t before = hal_i2c_get_transaction_count();
  float value = hal_mcp9600_read_thermocouple(&dev);
  TEST_ASSERT_TRUE(isnan(value));
  TEST_ASSERT_EQUAL_UINT32(before + 2u, hal_i2c_get_transaction_count());
}

void test_read_ambient_decodes_negative_temperature(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x00u, 0xFFu, 0x60u}; /* active, -10.0 C */
  hal_mock_i2c_inject_rx(rx, sizeof(rx));

  TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, hal_mcp9600_read_ambient(&dev));
}

void test_read_adc_sign_extends_24_bit_value(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0xFFu, 0x00u, 0x00u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));

  TEST_ASSERT_EQUAL_INT32(-65536, hal_mcp9600_read_adc(&dev));
}

void test_set_thermocouple_type_preserves_filter_bits(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x03u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_thermocouple_type(&dev, HAL_MCP9600_TYPE_J);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x05u, f[0]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x05u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x13u, f[1]);
}

void test_set_filter_preserves_type_bits(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x50u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_filter_coefficient(&dev, 4u);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x05u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x54u, f[1]);
}

void test_set_filter_masks_to_3_bits_per_datasheet(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x50u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_filter_coefficient(&dev, 0xFFu);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x05u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x57u, f[1]);
}

void test_adc_resolution_updates_device_config_bits(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x80u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_adc_resolution(&dev, HAL_MCP9600_ADC_RESOLUTION_14);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0xC0u, f[1]);
}

void test_get_adc_resolution_decodes_bits_6_5(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x40u}; /* bits [6:5] = 10b => 14-bit */
  hal_mock_i2c_inject_rx(rx, sizeof(rx));

  TEST_ASSERT_EQUAL_INT(HAL_MCP9600_ADC_RESOLUTION_14,
                        hal_mcp9600_get_adc_resolution(&dev));
}

void test_set_ambient_resolution_0_25_sets_bit7_and_keeps_adc_bits(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x40u}; /* preserve ADC bits [6:5]=10 */
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_ambient_resolution(&dev, HAL_MCP9600_AMBIENT_RES_0_25);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0xC0u, f[1]);
}

void test_set_ambient_resolution_0_0625_clears_bit7_and_keeps_adc_bits(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0xE0u}; /* bit7 set, ADC bits [6:5]=11 */
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_ambient_resolution(&dev, HAL_MCP9600_AMBIENT_RES_0_0625);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x60u, f[1]);
}

void test_enable_disable_updates_sleep_bits(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x00u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_enable(&dev, false);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01u, f[1]);
}

void test_enabled_reads_awake_state(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t rx[] = {0x00u};
  hal_mock_i2c_inject_rx(rx, sizeof(rx));

  TEST_ASSERT_TRUE(hal_mcp9600_enabled(&dev));
}

void test_alert_temperature_and_config_frames_match_original_driver(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_alert_temperature(&dev, 2u, 123.5f);
  hal_mcp9600_configure_alert(&dev, 2u, true, true, false, true, false);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(3, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x11u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x07u, f[1]);
  TEST_ASSERT_EQUAL_UINT8(0xB8u, f[2]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x09u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x0Du, f[1]);
}

void test_configure_alert_encodes_all_control_bits_per_register_5_11(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  hal_mock_i2c_reset_write_log();

  /* Register 5-11 bits:
   * bit4 Monitor TH/TC, bit3 Rise/Fall, bit2 Active level,
   * bit1 Comparator/Interrupt, bit0 Enable.
   */
  hal_mcp9600_configure_alert(&dev, 4u, true, true, true, true, true);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Bu, f[0]); /* ALERT4 config register */
  TEST_ASSERT_EQUAL_UINT8(0x1Fu, f[1]); /* bits 4..0 all set */
}

void test_configure_alert_invalid_index_does_not_write(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_configure_alert(&dev, 0u, true, true, true, true, true);
  hal_mcp9600_configure_alert(&dev, 5u, true, true, true, true, true);

  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_write_frame_count());
}

void test_status_flags_map_to_header_bit_masks(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));

  const uint8_t status_rx[] = {
      (uint8_t)(HAL_MCP9600_STATUS_BURST | HAL_MCP9600_STATUS_THUPDATE |
                HAL_MCP9600_STATUS_INPUTRANGE | HAL_MCP9600_STATUS_ALERT1 |
                HAL_MCP9600_STATUS_ALERT3)};
  hal_mock_i2c_inject_rx(status_rx, sizeof(status_rx));

  uint8_t status = hal_mcp9600_get_status(&dev);
  TEST_ASSERT_EQUAL_UINT8(status_rx[0], status);
  TEST_ASSERT_NOT_EQUAL_UINT8(0u, status & HAL_MCP9600_STATUS_BURST);
  TEST_ASSERT_NOT_EQUAL_UINT8(0u, status & HAL_MCP9600_STATUS_THUPDATE);
  TEST_ASSERT_NOT_EQUAL_UINT8(0u, status & HAL_MCP9600_STATUS_INPUTRANGE);
  TEST_ASSERT_NOT_EQUAL_UINT8(0u, status & HAL_MCP9600_STATUS_ALERT1);
  TEST_ASSERT_NOT_EQUAL_UINT8(0u, status & HAL_MCP9600_STATUS_ALERT3);
  TEST_ASSERT_EQUAL_UINT8(0u, status & HAL_MCP9600_STATUS_ALERT2);
  TEST_ASSERT_EQUAL_UINT8(0u, status & HAL_MCP9600_STATUS_ALERT4);
}

void test_get_alert_temperature_and_status(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  const uint8_t alert_rx[] = {0x07u, 0xB8u};
  hal_mock_i2c_inject_rx(alert_rx, sizeof(alert_rx));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.5f,
                           hal_mcp9600_get_alert_temperature(&dev, 2u));

  const uint8_t status_rx[] = {0x5Au};
  hal_mock_i2c_inject_rx(status_rx, sizeof(status_rx));
  TEST_ASSERT_EQUAL_UINT8(0x5Au, hal_mcp9600_get_status(&dev));
}

void test_set_alert_temperature_negative_value_is_encoded_twos_complement(
    void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  hal_mock_i2c_reset_write_log();

  hal_mcp9600_set_alert_temperature(&dev, 3u, -10.0f);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(3, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x12u, f[0]); /* TALERT3 */
  TEST_ASSERT_EQUAL_UINT8(0xFFu, f[1]);
  TEST_ASSERT_EQUAL_UINT8(0x60u, f[2]); /* -10.0 C / 0.0625 = -160 => 0xFF60 */
}

void test_alert_temperature_invalid_index_returns_nan_and_does_not_write(void) {
  TEST_ASSERT_TRUE(init_with_id(0x40u));
  hal_mock_i2c_reset_write_log();

  TEST_ASSERT_TRUE(isnan(hal_mcp9600_get_alert_temperature(&dev, 0u)));
  TEST_ASSERT_TRUE(isnan(hal_mcp9600_get_alert_temperature(&dev, 5u)));

  hal_mcp9600_set_alert_temperature(&dev, 0u, 25.0f);
  hal_mcp9600_set_alert_temperature(&dev, 5u, 25.0f);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_write_frame_count());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_accepts_mcp9600_and_writes_reset_config);
  RUN_TEST(test_init_accepts_mcp9601_device_id);
  RUN_TEST(test_init_rejects_unknown_device_id);
  RUN_TEST(test_read_thermocouple_decodes_signed_fixed_point);
  RUN_TEST(test_read_thermocouple_decodes_negative_twos_complement);
  RUN_TEST(test_read_thermocouple_returns_nan_when_sleeping);
  RUN_TEST(test_read_ambient_decodes_negative_temperature);
  RUN_TEST(test_read_adc_sign_extends_24_bit_value);
  RUN_TEST(test_set_thermocouple_type_preserves_filter_bits);
  RUN_TEST(test_set_filter_preserves_type_bits);
  RUN_TEST(test_set_filter_masks_to_3_bits_per_datasheet);
  RUN_TEST(test_adc_resolution_updates_device_config_bits);
  RUN_TEST(test_get_adc_resolution_decodes_bits_6_5);
  RUN_TEST(test_set_ambient_resolution_0_25_sets_bit7_and_keeps_adc_bits);
  RUN_TEST(test_set_ambient_resolution_0_0625_clears_bit7_and_keeps_adc_bits);
  RUN_TEST(test_enable_disable_updates_sleep_bits);
  RUN_TEST(test_enabled_reads_awake_state);
  RUN_TEST(test_alert_temperature_and_config_frames_match_original_driver);
  RUN_TEST(test_configure_alert_encodes_all_control_bits_per_register_5_11);
  RUN_TEST(test_configure_alert_invalid_index_does_not_write);
  RUN_TEST(test_status_flags_map_to_header_bit_masks);
  RUN_TEST(test_get_alert_temperature_and_status);
  RUN_TEST(
      test_set_alert_temperature_negative_value_is_encoded_twos_complement);
  RUN_TEST(test_alert_temperature_invalid_index_returns_nan_and_does_not_write);
  return UNITY_END();
}

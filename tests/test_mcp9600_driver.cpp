#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/mcp9600/mcp9600_driver.h"
#include "utils/unity.h"

#include <math.h>

static hal_mcp9600_t dev;

void setUp(void) {
    hal_i2c_init_bus(0, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ);
    hal_mock_i2c_set_busy(false);
    hal_mock_i2c_reset_write_log();
}

void tearDown(void) {
    hal_mcp9600_deinit(&dev);
}

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

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f,
                             hal_mcp9600_read_thermocouple(&dev));

    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
    TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
    TEST_ASSERT_EQUAL_INT(1, get_frame(1, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x00u, f[0]);
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

void test_ambient_resolution_preserves_original_inverted_bit_mapping(void) {
    TEST_ASSERT_TRUE(init_with_id(0x40u));
    const uint8_t rx[] = {0x00u};
    hal_mock_i2c_inject_rx(rx, sizeof(rx));
    hal_mock_i2c_reset_write_log();

    hal_mcp9600_set_ambient_resolution(&dev, HAL_MCP9600_AMBIENT_RES_0_125);

    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
    TEST_ASSERT_EQUAL_UINT8(0x80u, f[1]);
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_accepts_mcp9600_and_writes_reset_config);
    RUN_TEST(test_init_accepts_mcp9601_device_id);
    RUN_TEST(test_init_rejects_unknown_device_id);
    RUN_TEST(test_read_thermocouple_decodes_signed_fixed_point);
    RUN_TEST(test_read_thermocouple_returns_nan_when_sleeping);
    RUN_TEST(test_read_ambient_decodes_negative_temperature);
    RUN_TEST(test_read_adc_sign_extends_24_bit_value);
    RUN_TEST(test_set_thermocouple_type_preserves_filter_bits);
    RUN_TEST(test_set_filter_preserves_type_bits);
    RUN_TEST(test_adc_resolution_updates_device_config_bits);
    RUN_TEST(test_ambient_resolution_preserves_original_inverted_bit_mapping);
    RUN_TEST(test_enable_disable_updates_sleep_bits);
    RUN_TEST(test_enabled_reads_awake_state);
    RUN_TEST(test_alert_temperature_and_config_frames_match_original_driver);
    RUN_TEST(test_get_alert_temperature_and_status);
    return UNITY_END();
}

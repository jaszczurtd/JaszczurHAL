#include "utils/unity.h"
#include "hal/hal_thermocouple.h"
#include "hal/impl/.mock/hal_mock.h"
#include <math.h>

#ifndef HAL_DISABLE_MCP9600
static hal_thermocouple_t mcp;
#endif
#ifndef HAL_DISABLE_MAX6675
static hal_thermocouple_t max;
#endif

#ifndef HAL_DISABLE_MCP9600
static hal_thermocouple_config_t mcp_cfg(void) {
    hal_thermocouple_config_t cfg = {};
    cfg.chip        = HAL_THERMOCOUPLE_CHIP_MCP9600;
    cfg.bus.i2c     = {4, 5, 400000, 0, 0x67};
    return cfg;
}
#endif

#ifndef HAL_DISABLE_MAX6675
static hal_thermocouple_config_t max_cfg(void) {
    hal_thermocouple_config_t cfg = {};
    cfg.chip        = HAL_THERMOCOUPLE_CHIP_MAX6675;
    cfg.bus.spi     = {2, 3, 4};
    return cfg;
}
#endif

void setUp(void) {
#ifndef HAL_DISABLE_MCP9600
    hal_thermocouple_config_t c = mcp_cfg();
    mcp = hal_thermocouple_init(&c);
#endif
#ifndef HAL_DISABLE_MAX6675
    hal_thermocouple_config_t x = max_cfg();
    max = hal_thermocouple_init(&x);
#endif
}

void tearDown(void) {
#ifndef HAL_DISABLE_MCP9600
    hal_thermocouple_deinit(mcp); mcp = nullptr;
#endif
#ifndef HAL_DISABLE_MAX6675
    hal_thermocouple_deinit(max); max = nullptr;
#endif
}

void test_init_returns_valid_handles(void) {
#ifndef HAL_DISABLE_MCP9600
    TEST_ASSERT_NOT_NULL(mcp);
#endif
#ifndef HAL_DISABLE_MAX6675
    TEST_ASSERT_NOT_NULL(max);
#endif
}

#ifndef HAL_DISABLE_MCP9600
void test_mcp_default_temp(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, hal_thermocouple_read(mcp));
}
#endif

#ifndef HAL_DISABLE_MAX6675
void test_max_default_temp(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, hal_thermocouple_read(max));
}
#endif

#ifndef HAL_DISABLE_MCP9600
void test_mcp_inject_temp(void) {
    hal_mock_thermocouple_set_temp(mcp, 150.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.5f, hal_thermocouple_read(mcp));
}

void test_mcp_inject_ambient(void) {
    hal_mock_thermocouple_set_ambient(mcp, 30.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, hal_thermocouple_read_ambient(mcp));
}
#endif

#if !defined(HAL_DISABLE_MAX6675) && !defined(HAL_DISABLE_MCP9600)
void test_max_ambient_unsupported_returns_nan(void) {
    TEST_ASSERT_TRUE(isnan(hal_thermocouple_read_ambient(max)));
}
#endif

#ifndef HAL_DISABLE_MCP9600
void test_mcp_set_get_type(void) {
    hal_thermocouple_set_type(mcp, HAL_THERMOCOUPLE_TYPE_J);
    TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_TYPE_J, hal_thermocouple_get_type(mcp));
}
#endif

#ifndef HAL_DISABLE_MAX6675
void test_max_type_always_k(void) {
    TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_TYPE_K, hal_thermocouple_get_type(max));
}
#endif

#ifndef HAL_DISABLE_MCP9600
void test_mcp_set_get_filter(void) {
    hal_thermocouple_set_filter(mcp, 4);
    TEST_ASSERT_EQUAL_UINT8(4, hal_thermocouple_get_filter(mcp));
}

void test_mcp_set_get_adc_resolution(void) {
    hal_thermocouple_set_adc_resolution(mcp, HAL_THERMOCOUPLE_ADC_RES_14);
    TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_ADC_RES_14, hal_thermocouple_get_adc_resolution(mcp));
}

void test_mcp_enable_disable_cycle(void) {
    TEST_ASSERT_TRUE(hal_thermocouple_is_enabled(mcp));

    hal_thermocouple_enable(mcp, false);
    TEST_ASSERT_FALSE(hal_thermocouple_is_enabled(mcp));

    hal_thermocouple_enable(mcp, true);
    TEST_ASSERT_TRUE(hal_thermocouple_is_enabled(mcp));
}

void test_mcp_set_alert_and_read_status(void) {
    hal_thermocouple_alert_cfg_t cfg = {};
    cfg.temperature = 123.5f;
    cfg.rising = true;
    cfg.alert_cold_junction = false;
    cfg.active_high = true;
    cfg.interrupt_mode = false;

    hal_thermocouple_set_alert(mcp, 2, true, &cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.5f, hal_thermocouple_get_alert_temp(mcp, 2));

    hal_mock_thermocouple_set_status(mcp, 0x5A);
    TEST_ASSERT_EQUAL_UINT8(0x5A, hal_thermocouple_get_status(mcp));
}

void test_mcp_set_ambient_resolution_does_not_break_reads(void) {
    hal_mock_thermocouple_set_ambient(mcp, 26.25f);
    hal_thermocouple_set_ambient_resolution(mcp, HAL_THERMOCOUPLE_AMBIENT_RES_0_125);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.25f, hal_thermocouple_read_ambient(mcp));
}
#endif

#ifndef HAL_DISABLE_MAX6675
void test_max_is_always_enabled(void) {
    TEST_ASSERT_TRUE(hal_thermocouple_is_enabled(max));
}
#endif

void test_null_handle_read_returns_nan(void) {
    TEST_ASSERT_TRUE(isnan(hal_thermocouple_read(nullptr)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_returns_valid_handles);
#ifndef HAL_DISABLE_MCP9600
    RUN_TEST(test_mcp_default_temp);
#endif
#ifndef HAL_DISABLE_MAX6675
    RUN_TEST(test_max_default_temp);
#endif
#ifndef HAL_DISABLE_MCP9600
    RUN_TEST(test_mcp_inject_temp);
    RUN_TEST(test_mcp_inject_ambient);
#endif
#if !defined(HAL_DISABLE_MAX6675) && !defined(HAL_DISABLE_MCP9600)
    RUN_TEST(test_max_ambient_unsupported_returns_nan);
#endif
#ifndef HAL_DISABLE_MCP9600
    RUN_TEST(test_mcp_set_get_type);
#endif
#ifndef HAL_DISABLE_MAX6675
    RUN_TEST(test_max_type_always_k);
#endif
#ifndef HAL_DISABLE_MCP9600
    RUN_TEST(test_mcp_set_get_filter);
    RUN_TEST(test_mcp_set_get_adc_resolution);
    RUN_TEST(test_mcp_enable_disable_cycle);
    RUN_TEST(test_mcp_set_alert_and_read_status);
    RUN_TEST(test_mcp_set_ambient_resolution_does_not_break_reads);
#endif
#ifndef HAL_DISABLE_MAX6675
    RUN_TEST(test_max_is_always_enabled);
#endif
    RUN_TEST(test_null_handle_read_returns_nan);
    return UNITY_END();
}

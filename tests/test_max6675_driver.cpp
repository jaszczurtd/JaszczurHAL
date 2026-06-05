#include "utils/unity.h"
#include "hal/impl/shared/max6675_driver.h"
#include "hal/impl/.mock/hal_mock.h"

#include <math.h>
#include <stdint.h>

#define MAX_SCLK 2u
#define MAX_CS   3u
#define MAX_MISO 4u

static hal_max6675_t dev;

static hal_max6675_config_t cfg(void) {
    hal_max6675_config_t c = {};
    c.sclk_pin = MAX_SCLK;
    c.cs_pin = MAX_CS;
    c.miso_pin = MAX_MISO;
    return c;
}

static void push_raw(uint16_t raw) {
    bool bits[16] = {};
    for (int bit = 15; bit >= 0; --bit) {
        bits[15 - bit] = ((raw >> bit) & 0x1u) != 0u;
    }
    hal_mock_gpio_push_read_sequence(MAX_MISO, bits, 16u);
}

void setUp(void) {
    dev = {};
    hal_mock_gpio_clear_read_sequence(MAX_MISO);
}

void tearDown(void) {
    hal_mock_gpio_clear_read_sequence(MAX_MISO);
}

void test_init_configures_gpio_and_idle_levels(void) {
    hal_max6675_config_t c = cfg();

    TEST_ASSERT_TRUE(hal_max6675_init(&dev, &c));
    TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(MAX_CS));
    TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(MAX_SCLK));
    TEST_ASSERT_EQUAL_INT(HAL_GPIO_INPUT, hal_mock_gpio_get_mode(MAX_MISO));
    TEST_ASSERT_TRUE(hal_mock_gpio_get_state(MAX_CS));
    hal_max6675_deinit(&dev);
}

void test_raw_to_celsius_decodes_quarter_degree_steps(void) {
    const uint16_t raw = (uint16_t)(100u << 3); /* 100 * 0.25 C = 25.0 C */

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, hal_max6675_raw_to_celsius(raw));
}

void test_raw_to_celsius_returns_nan_on_open_circuit(void) {
    TEST_ASSERT_TRUE(hal_max6675_raw_has_fault(0x0004u));
    TEST_ASSERT_TRUE(isnan(hal_max6675_raw_to_celsius(0x0004u)));
}

void test_read_raw_bitbangs_msb_first_and_restores_idle_levels(void) {
    hal_max6675_config_t c = cfg();
    TEST_ASSERT_TRUE(hal_max6675_init(&dev, &c));

    push_raw(0x0320u);
    TEST_ASSERT_EQUAL_HEX16(0x0320u, hal_max6675_read_raw(&dev));
    TEST_ASSERT_TRUE(hal_mock_gpio_get_state(MAX_CS));
    TEST_ASSERT_TRUE(hal_mock_gpio_get_state(MAX_SCLK));
    hal_max6675_deinit(&dev);
}

void test_read_celsius_uses_bitbang_raw_value(void) {
    hal_max6675_config_t c = cfg();
    TEST_ASSERT_TRUE(hal_max6675_init(&dev, &c));

    push_raw(0x0320u);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, hal_max6675_read_celsius(&dev));
    hal_max6675_deinit(&dev);
}

void test_read_celsius_returns_nan_for_scripted_fault(void) {
    hal_max6675_config_t c = cfg();
    TEST_ASSERT_TRUE(hal_max6675_init(&dev, &c));

    push_raw(0x0004u);
    TEST_ASSERT_TRUE(isnan(hal_max6675_read_celsius(&dev)));
    hal_max6675_deinit(&dev);
}

void test_read_celsius_returns_nan_for_null_device(void) {
    TEST_ASSERT_TRUE(isnan(hal_max6675_read_celsius(nullptr)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_gpio_and_idle_levels);
    RUN_TEST(test_raw_to_celsius_decodes_quarter_degree_steps);
    RUN_TEST(test_raw_to_celsius_returns_nan_on_open_circuit);
    RUN_TEST(test_read_raw_bitbangs_msb_first_and_restores_idle_levels);
    RUN_TEST(test_read_celsius_uses_bitbang_raw_value);
    RUN_TEST(test_read_celsius_returns_nan_for_scripted_fault);
    RUN_TEST(test_read_celsius_returns_nan_for_null_device);
    return UNITY_END();
}

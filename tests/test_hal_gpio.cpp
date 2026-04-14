#include "utils/unity.h"
#include "hal/hal_gpio.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {}
void tearDown(void) {}

void test_set_mode_output(void) {
    hal_gpio_set_mode(5, HAL_GPIO_OUTPUT);
    TEST_ASSERT_TRUE(hal_mock_gpio_is_output(5));
    TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(5));
}

void test_set_mode_input(void) {
    hal_gpio_set_mode(3, HAL_GPIO_INPUT);
    TEST_ASSERT_FALSE(hal_mock_gpio_is_output(3));
    TEST_ASSERT_EQUAL_INT(HAL_GPIO_INPUT, hal_mock_gpio_get_mode(3));
}

void test_write_high(void) {
    hal_gpio_set_mode(10, HAL_GPIO_OUTPUT);
    hal_gpio_write(10, true);
    TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10));
}

void test_write_low(void) {
    hal_gpio_set_mode(10, HAL_GPIO_OUTPUT);
    hal_gpio_write(10, true);
    hal_gpio_write(10, false);
    TEST_ASSERT_FALSE(hal_mock_gpio_get_state(10));
}

void test_read_injected_high(void) {
    hal_gpio_set_mode(7, HAL_GPIO_INPUT);
    hal_mock_gpio_inject_level(7, true);
    TEST_ASSERT_TRUE(hal_gpio_read(7));
}

void test_read_injected_low(void) {
    hal_gpio_set_mode(7, HAL_GPIO_INPUT);
    hal_mock_gpio_inject_level(7, false);
    TEST_ASSERT_FALSE(hal_gpio_read(7));
}

void test_default_state_is_low(void) {
    TEST_ASSERT_FALSE(hal_gpio_read(63));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_mode_output);
    RUN_TEST(test_set_mode_input);
    RUN_TEST(test_write_high);
    RUN_TEST(test_write_low);
    RUN_TEST(test_read_injected_high);
    RUN_TEST(test_read_injected_low);
    RUN_TEST(test_default_state_is_low);
    return UNITY_END();
}

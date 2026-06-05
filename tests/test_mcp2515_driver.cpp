#include "utils/unity.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/mcp2515/mcp2515_driver.h"

void setUp(void) {
    hal_mock_spi_reset();
    hal_mock_set_millis(0);
    hal_mock_set_micros(0);
}

void tearDown(void) {}

void test_set_gpo_uses_hal_spi_and_configures_cs_pin(void) {
    JHMCP2515 can(10u, 0u);

    TEST_ASSERT_EQUAL_UINT8(0u, can.setGPO(1u));
    TEST_ASSERT_TRUE(hal_mock_spi_is_initialized());
    TEST_ASSERT_EQUAL_UINT8(0u, hal_mock_spi_get_bus());
    TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(10u));
    TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));

    uint8_t tx[16] = {};
    size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
    TEST_ASSERT_EQUAL_size_t(4u, tx_len);
    TEST_ASSERT_EQUAL_UINT8(MCP_BITMOD, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(MCP_BFPCTRL, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(MCP_BxBFS_MASK, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(0x10u, tx[3]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_gpo_uses_hal_spi_and_configures_cs_pin);
    return UNITY_END();
}
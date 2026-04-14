#include "utils/unity.h"
#include "hal/hal_swserial.h"
#include "hal/impl/.mock/hal_mock.h"

static hal_swserial_t s_port = NULL;

void setUp(void) {
    s_port = hal_swserial_create(5, 4);
    hal_mock_swserial_reset(s_port);
}

void tearDown(void) {
    hal_swserial_destroy(s_port);
    s_port = NULL;
}

void test_swserial_reads_injected_bytes(void) {
    const uint8_t payload[] = {'O', 'K'};
    hal_mock_swserial_push(s_port, payload, (int)sizeof(payload));

    TEST_ASSERT_EQUAL_INT(2, hal_swserial_available(s_port));
    TEST_ASSERT_EQUAL_INT('O', hal_swserial_read(s_port));
    TEST_ASSERT_EQUAL_INT('K', hal_swserial_read(s_port));
    TEST_ASSERT_EQUAL_INT(-1, hal_swserial_read(s_port));
}

void test_swserial_captures_written_line(void) {
    TEST_ASSERT_EQUAL_UINT32(4u, hal_swserial_println(s_port, "ATE0"));
    TEST_ASSERT_EQUAL_STRING("ATE0", hal_mock_swserial_last_write(s_port));
}

void test_swserial_reassigns_pins_before_begin(void) {
    TEST_ASSERT_TRUE(hal_swserial_set_rx(s_port, 7));
    TEST_ASSERT_TRUE(hal_swserial_set_tx(s_port, 6));

    hal_swserial_begin(s_port, 115200, HAL_UART_CFG_8N1);

    TEST_ASSERT_FALSE(hal_swserial_set_rx(s_port, 9));
    TEST_ASSERT_FALSE(hal_swserial_set_tx(s_port, 8));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_swserial_reads_injected_bytes);
    RUN_TEST(test_swserial_captures_written_line);
    RUN_TEST(test_swserial_reassigns_pins_before_begin);
    return UNITY_END();
}
#include "utils/unity.h"
#include "hal/hal_uart.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

static hal_uart_t s_uart = NULL;

void setUp(void) {
    s_uart = hal_uart_create(HAL_UART_PORT_2, 5, 4);
    hal_mock_uart_reset(s_uart);
}

void tearDown(void) {
    hal_uart_destroy(s_uart);
    s_uart = NULL;
}

/* ── basic read / write ─────────────────────────────────────────────────── */

void test_uart_reads_injected_bytes(void) {
    const uint8_t payload[] = {'A', 'T'};
    hal_mock_uart_push(s_uart, payload, (int)sizeof(payload));

    TEST_ASSERT_EQUAL_INT(2, hal_uart_available(s_uart));
    TEST_ASSERT_EQUAL_INT('A', hal_uart_read(s_uart));
    TEST_ASSERT_EQUAL_INT('T', hal_uart_read(s_uart));
    TEST_ASSERT_EQUAL_INT(-1, hal_uart_read(s_uart));
}

void test_uart_captures_written_line(void) {
    size_t n = hal_uart_println(s_uart, "AT");
    /* println adds \r\n → "AT\r\n" = 4 bytes */
    TEST_ASSERT_EQUAL_UINT32(4u, n);
    TEST_ASSERT_EQUAL_STRING("AT\r\n", hal_mock_uart_last_write(s_uart));
}

void test_uart_reassigns_pins(void) {
    TEST_ASSERT_TRUE(hal_uart_set_rx(s_uart, 9));
    TEST_ASSERT_TRUE(hal_uart_set_tx(s_uart, 8));
    TEST_ASSERT_EQUAL_UINT8(9u, hal_mock_uart_get_rx_pin(s_uart));
    TEST_ASSERT_EQUAL_UINT8(8u, hal_mock_uart_get_tx_pin(s_uart));
}

/* ── NULL handle safety ─────────────────────────────────────────────────── */

void test_uart_null_handle_returns_safe_defaults(void) {
    TEST_ASSERT_EQUAL_INT(0, hal_uart_available(NULL));
    TEST_ASSERT_EQUAL_INT(-1, hal_uart_read(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_uart_write(NULL, (const uint8_t *)"x", 1));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_uart_println(NULL, "x"));
    TEST_ASSERT_FALSE(hal_uart_set_rx(NULL, 1));
    TEST_ASSERT_FALSE(hal_uart_set_tx(NULL, 1));
    /* should not crash */
    hal_uart_flush(NULL);
    hal_uart_destroy(NULL);
}

/* ── write edge cases ───────────────────────────────────────────────────── */

void test_uart_write_null_data_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, hal_uart_write(s_uart, NULL, 5));
}

void test_uart_write_zero_len_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, hal_uart_write(s_uart, (const uint8_t *)"AB", 0));
}

void test_uart_write_returns_clamped_length(void) {
    /* write exactly 10 bytes – should return 10 */
    const uint8_t data[10] = {'0','1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_UINT32(10u, hal_uart_write(s_uart, data, 10));
    TEST_ASSERT_EQUAL_STRING("0123456789", hal_mock_uart_last_write(s_uart));
}

/* ── println edge cases ─────────────────────────────────────────────────── */

void test_uart_println_null_string(void) {
    size_t n = hal_uart_println(s_uart, NULL);
    /* empty string + \r\n */
    TEST_ASSERT_EQUAL_UINT32(2u, n);
    TEST_ASSERT_EQUAL_STRING("\r\n", hal_mock_uart_last_write(s_uart));
}

/* ── begin (no crash) ───────────────────────────────────────────────────── */

void test_uart_begin_does_not_crash(void) {
    hal_uart_begin(s_uart, 115200, HAL_UART_CFG_8N1);
    hal_uart_begin(NULL, 9600, HAL_UART_CFG_8N1);
}

/* ── flush (no crash) ───────────────────────────────────────────────────── */

void test_uart_flush_does_not_crash(void) {
    hal_uart_flush(s_uart);
}

/* ── create / destroy cycling ───────────────────────────────────────────── */

void test_uart_create_destroy_recycles_slot(void) {
    /* Destroy the one from setUp, then create two new ones. */
    hal_uart_destroy(s_uart);
    s_uart = NULL;

    hal_uart_t a = hal_uart_create(HAL_UART_PORT_1, 0, 1);
    TEST_ASSERT_NOT_NULL(a);
    hal_uart_destroy(a);

    /* Same port should work again after destroy. */
    hal_uart_t b = hal_uart_create(HAL_UART_PORT_1, 2, 3);
    TEST_ASSERT_NOT_NULL(b);
    hal_uart_destroy(b);

    /* Re-create for tearDown */
    s_uart = hal_uart_create(HAL_UART_PORT_2, 5, 4);
}

/* ── ring buffer overflow ───────────────────────────────────────────────── */

void test_uart_ring_buffer_overflow_drops_excess(void) {
    /* Push 512 bytes into a 511-capacity ring buffer → last byte dropped */
    uint8_t big[512];
    memset(big, 0xAA, sizeof(big));
    hal_mock_uart_push(s_uart, big, (int)sizeof(big));

    /* Ring buffer max usable = BUF_SIZE-1 = 511 */
    TEST_ASSERT_EQUAL_INT(511, hal_uart_available(s_uart));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_uart_reads_injected_bytes);
    RUN_TEST(test_uart_captures_written_line);
    RUN_TEST(test_uart_reassigns_pins);
    RUN_TEST(test_uart_null_handle_returns_safe_defaults);
    RUN_TEST(test_uart_write_null_data_returns_zero);
    RUN_TEST(test_uart_write_zero_len_returns_zero);
    RUN_TEST(test_uart_write_returns_clamped_length);
    RUN_TEST(test_uart_println_null_string);
    RUN_TEST(test_uart_begin_does_not_crash);
    RUN_TEST(test_uart_flush_does_not_crash);
    RUN_TEST(test_uart_create_destroy_recycles_slot);
    RUN_TEST(test_uart_ring_buffer_overflow_drops_excess);
    return UNITY_END();
}
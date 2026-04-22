#include "utils/unity.h"
#include "hal/hal_serial_session.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

void setUp(void) {
    hal_debug_init(115200);
    hal_mock_serial_reset();
    hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_session_init_clears_state(void) {
    hal_serial_session_t s;
    memset(&s, 0xAA, sizeof(s));

    hal_serial_session_init(&s);

    TEST_ASSERT_FALSE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_serial_session_id(&s));
    TEST_ASSERT_EQUAL_UINT32(0u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(0u, s.last_activity_ms);
    TEST_ASSERT_EQUAL_UINT8(0u, s.line_len);
}

void test_poll_hello_activates_session_and_replies(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s);
    hal_mock_set_millis(1234);
    hal_mock_serial_inject_rx("HELLO\n", -1);

    hal_serial_session_poll(&s, "ECU");

    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(1u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(1234u, s.last_activity_ms);
    TEST_ASSERT_EQUAL_UINT32((((uint32_t)1u << 20) ^ 1234u), hal_serial_session_id(&s));
    TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "OK HELLO module=ECU proto=1"));
}

void test_poll_unknown_command_returns_error(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s);
    hal_mock_serial_inject_rx("PING\n", -1);

    hal_serial_session_poll(&s, "ECU");

    TEST_ASSERT_FALSE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN", hal_mock_serial_last_line());
}

void test_poll_handles_multiple_commands_in_one_rx_buffer(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s);
    hal_mock_serial_inject_rx("HELLO\r\nHELLO\n", -1);

    hal_serial_session_poll(&s, "ECU");

    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(2u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(((uint32_t)2u << 20), hal_serial_session_id(&s));
}

void test_poll_null_args_is_safe(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s);
    hal_mock_serial_inject_rx("HELLO\n", -1);

    hal_serial_session_poll(NULL, "ECU");
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());

    hal_serial_session_poll(&s, NULL);
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_session_init_clears_state);
    RUN_TEST(test_poll_hello_activates_session_and_replies);
    RUN_TEST(test_poll_unknown_command_returns_error);
    RUN_TEST(test_poll_handles_multiple_commands_in_one_rx_buffer);
    RUN_TEST(test_poll_null_args_is_safe);
    return UNITY_END();
}

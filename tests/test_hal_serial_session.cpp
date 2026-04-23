#include "utils/unity.h"
#include "hal/hal_serial_session.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

void setUp(void) {
    hal_debug_init(115200);
    hal_mock_serial_reset();
    hal_mock_set_millis(0);
    hal_mock_reset_device_uid();
}

void tearDown(void) {}

void test_session_init_clears_state_and_binds_identity(void) {
    hal_serial_session_t s;
    memset(&s, 0xAA, sizeof(s));

    hal_serial_session_init(&s, "ECU", "1.2.3", "abc123");

    TEST_ASSERT_FALSE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_serial_session_id(&s));
    TEST_ASSERT_EQUAL_UINT32(0u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(0u, s.last_activity_ms);
    TEST_ASSERT_EQUAL_UINT8(0u, s.line_len);
    TEST_ASSERT_EQUAL_STRING("ECU",    s.module_tag);
    TEST_ASSERT_EQUAL_STRING("1.2.3",  s.fw_version);
    TEST_ASSERT_EQUAL_STRING("abc123", s.build_id);
    TEST_ASSERT_EQUAL_STRING("E661A4D1234567AB", s.uid_hex); /* default mock UID */
}

void test_session_init_nullable_identity_defaults_to_unknown(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("unknown", s.fw_version);
    TEST_ASSERT_EQUAL_STRING("unknown", s.build_id);

    hal_serial_session_init(&s, "ECU", "", "");
    TEST_ASSERT_EQUAL_STRING("unknown", s.fw_version);
    TEST_ASSERT_EQUAL_STRING("unknown", s.build_id);
}

void test_poll_hello_reports_full_identity(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", "1.2.3", "abc123");
    hal_mock_set_millis(1234);
    hal_mock_serial_inject_rx("HELLO\n", -1);

    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(1u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(1234u, s.last_activity_ms);
    TEST_ASSERT_EQUAL_UINT32((((uint32_t)1u << 20) ^ 1234u), hal_serial_session_id(&s));

    const char *line = hal_mock_serial_last_line();
    TEST_ASSERT_NOT_NULL(strstr(line, "OK HELLO"));
    TEST_ASSERT_NOT_NULL(strstr(line, "module=ECU"));
    TEST_ASSERT_NOT_NULL(strstr(line, "proto=1"));
    TEST_ASSERT_NOT_NULL(strstr(line, "fw=1.2.3"));
    TEST_ASSERT_NOT_NULL(strstr(line, "build=abc123"));
    TEST_ASSERT_NOT_NULL(strstr(line, "uid=E661A4D1234567AB"));
}

void test_poll_hello_with_unknown_identity(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", NULL, NULL);
    hal_mock_serial_inject_rx("HELLO\n", -1);

    hal_serial_session_poll(&s);

    const char *line = hal_mock_serial_last_line();
    TEST_ASSERT_NOT_NULL(strstr(line, "fw=unknown"));
    TEST_ASSERT_NOT_NULL(strstr(line, "build=unknown"));
}

void test_poll_uses_injected_mock_uid(void) {
    const uint8_t custom_uid[HAL_DEVICE_UID_BYTES] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };
    hal_mock_set_device_uid(custom_uid);

    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    hal_mock_serial_inject_rx("HELLO\n", -1);

    hal_serial_session_poll(&s);

    TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "uid=0102030405060708"));
}

void test_poll_unknown_command_returns_error(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    hal_mock_serial_inject_rx("PING\n", -1);

    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN", hal_mock_serial_last_line());
}

void test_poll_handles_multiple_commands_in_one_rx_buffer(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    hal_mock_serial_inject_rx("HELLO\r\nHELLO\n", -1);

    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(2u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(((uint32_t)2u << 20), hal_serial_session_id(&s));
}

void test_poll_null_args_is_safe(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    hal_mock_serial_inject_rx("HELLO\n", -1);

    hal_serial_session_poll(NULL);
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());

    /* Session with NULL module_tag is defensively rejected. */
    hal_serial_session_t bad;
    memset(&bad, 0, sizeof(bad));
    hal_serial_session_poll(&bad);
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_session_init_clears_state_and_binds_identity);
    RUN_TEST(test_session_init_nullable_identity_defaults_to_unknown);
    RUN_TEST(test_poll_hello_reports_full_identity);
    RUN_TEST(test_poll_hello_with_unknown_identity);
    RUN_TEST(test_poll_uses_injected_mock_uid);
    RUN_TEST(test_poll_unknown_command_returns_error);
    RUN_TEST(test_poll_handles_multiple_commands_in_one_rx_buffer);
    RUN_TEST(test_poll_null_args_is_safe);
    return UNITY_END();
}

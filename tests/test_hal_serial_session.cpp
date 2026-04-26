#include "utils/unity.h"
#include "hal/hal_serial_session.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

static hal_serial_session_t *g_unknown_session = NULL;
static char g_unknown_line[HAL_SERIAL_SESSION_MAX_LINE + 1u];
static void *g_unknown_user = NULL;
static uint32_t g_unknown_calls = 0u;

static void reset_unknown_capture(void) {
    g_unknown_session = NULL;
    g_unknown_line[0] = '\0';
    g_unknown_user = NULL;
    g_unknown_calls = 0u;
}

static bool build_framed_line(uint16_t seq,
                              const char *payload,
                              char *out,
                              size_t out_size,
                              char terminator) {
    size_t frame_len = 0u;
    if (!hal_serial_frame_encode(seq, payload, out, out_size, &frame_len)) {
        return false;
    }
    if (frame_len + 2u > out_size) {
        return false;
    }
    out[frame_len] = terminator;
    out[frame_len + 1u] = '\0';
    return true;
}

static void inject_framed_line(uint16_t seq, const char *payload, char terminator) {
    char line[HAL_SERIAL_FRAME_LINE_MAX + 2u];
    TEST_ASSERT_TRUE(build_framed_line(seq, payload, line, sizeof(line), terminator));
    hal_mock_serial_inject_rx(line, -1);
}

static bool decode_last_framed_reply(uint16_t *seq_out,
                                     char *payload_out,
                                     size_t payload_out_size) {
    return hal_serial_frame_decode(hal_mock_serial_last_line(),
                                   seq_out,
                                   payload_out,
                                   payload_out_size);
}

static void unknown_reply_cb(const char *line, void *user) {
    g_unknown_calls++;
    g_unknown_user = user;

    if (line != NULL) {
        strncpy(g_unknown_line, line, sizeof(g_unknown_line) - 1u);
        g_unknown_line[sizeof(g_unknown_line) - 1u] = '\0';
    }

    if (g_unknown_session != NULL) {
        hal_serial_session_println(g_unknown_session, "SC_OK META source=session");
    }
}

static void noisy_unknown_cb(const char *line, void *user) {
    (void)line;
    (void)user;

    if (g_unknown_session != NULL) {
        hal_serial_session_println(g_unknown_session, "SC_OK NOISE");
    }

    /* Must be suppressed while framed request dispatch is in progress. */
    hal_deb("debug-noise");
    hal_derr("error-noise");
}

void setUp(void) {
    hal_debug_init(115200);
    hal_debug_set_muted(false);
    hal_mock_serial_reset();
    hal_mock_set_millis(0);
    hal_mock_reset_device_uid();
    reset_unknown_capture();
}

void tearDown(void) {}

void test_frame_crc8_matches_reference_vector(void) {
    TEST_ASSERT_EQUAL_UINT8(0xF4u,
                            hal_serial_frame_crc8((const uint8_t *)"123456789", 9u));
}

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
    TEST_ASSERT_NULL(s.unknown_handler);
    TEST_ASSERT_NULL(s.unknown_user);
    TEST_ASSERT_FALSE(s.in_request);
    TEST_ASSERT_EQUAL_UINT16(0u, s.request_seq);
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

void test_poll_hello_reports_full_identity_and_seq_echo(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[192];

    hal_serial_session_init(&s, "ECU", "1.2.3", "abc123");
    hal_mock_set_millis(1234);
    inject_framed_line(42u, "HELLO", '\n');

    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(1u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(1234u, s.last_activity_ms);
    TEST_ASSERT_EQUAL_UINT32((((uint32_t)1u << 20) ^ 1234u), hal_serial_session_id(&s));
    TEST_ASSERT_FALSE(hal_debug_is_muted());

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(42u, reply_seq);

    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "OK HELLO"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "module=ECU"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "proto=1"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "fw=1.2.3"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "build=abc123"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "uid=E661A4D1234567AB"));
}

void test_poll_hello_with_unknown_identity(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[192];

    hal_serial_session_init(&s, "ECU", NULL, NULL);
    inject_framed_line(7u, "HELLO", '\n');

    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(7u, reply_seq);
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "fw=unknown"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "build=unknown"));
}

void test_poll_uses_injected_mock_uid(void) {
    const uint8_t custom_uid[HAL_DEVICE_UID_BYTES] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };
    uint16_t reply_seq = 0u;
    char reply_payload[192];

    hal_mock_set_device_uid(custom_uid);

    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(11u, "HELLO", '\n');

    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(11u, reply_seq);
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "uid=0102030405060708"));
}

void test_poll_unknown_payload_without_handler_returns_sc_unknown_cmd(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[HAL_SERIAL_SESSION_MAX_LINE + 1u];

    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(9u, "PING", '\n');

    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_serial_session_is_active(&s));
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(9u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", reply_payload);
}

void test_poll_unknown_payload_with_handler_dispatches_inner_and_reply_seq(void) {
    hal_serial_session_t s;
    int user_tag = 17;
    uint16_t reply_seq = 0u;
    char reply_payload[HAL_SERIAL_SESSION_MAX_LINE + 1u];

    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    g_unknown_session = &s;
    hal_serial_session_set_unknown_handler(&s, unknown_reply_cb, &user_tag);
    inject_framed_line(333u, "SC_GET_META", '\n');

    hal_serial_session_poll(&s);

    TEST_ASSERT_EQUAL_UINT32(1u, g_unknown_calls);
    TEST_ASSERT_EQUAL_PTR(&user_tag, g_unknown_user);
    TEST_ASSERT_EQUAL_STRING("SC_GET_META", g_unknown_line);

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(333u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_OK META source=session", reply_payload);
}

void test_poll_drops_non_framed_and_bad_crc_lines(void) {
    hal_serial_session_t s;
    char bad_crc_line[HAL_SERIAL_FRAME_LINE_MAX + 2u];
    size_t len = 0u;

    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    hal_mock_serial_inject_rx("HELLO\nPING\n", -1);

    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());

    TEST_ASSERT_TRUE(hal_serial_frame_encode(1u, "HELLO", bad_crc_line,
                                             sizeof(bad_crc_line), &len));
    TEST_ASSERT_TRUE(len >= 2u);
    bad_crc_line[len - 1u] = (bad_crc_line[len - 1u] == '0') ? '1' : '0';
    bad_crc_line[len] = '\n';
    bad_crc_line[len + 1u] = '\0';
    hal_mock_serial_inject_rx(bad_crc_line, -1);

    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(0u, s.hello_counter);
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
    TEST_ASSERT_FALSE(hal_debug_is_muted());
}

void test_poll_mutes_debug_during_framed_dispatch_and_restores_state(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[HAL_SERIAL_SESSION_MAX_LINE + 1u];

    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    g_unknown_session = &s;
    hal_serial_session_set_unknown_handler(&s, noisy_unknown_cb, NULL);
    inject_framed_line(77u, "SC_NOISY", '\n');

    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(77u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_OK NOISE", reply_payload);
    TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
    TEST_ASSERT_FALSE(hal_debug_is_muted());
}

void test_hal_serial_session_println_is_gated_by_request_window(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[HAL_SERIAL_SESSION_MAX_LINE + 1u];

    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    hal_serial_session_println(&s, "SC_SHOULD_NOT_EMIT");
    TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());

    s.in_request = true;
    s.request_seq = 123u;
    hal_serial_session_println(&s, "SC_OK DIRECT");

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(123u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_OK DIRECT", reply_payload);
}

void test_poll_handles_multiple_framed_commands_in_one_rx_buffer(void) {
    hal_serial_session_t s;
    char frame_a[HAL_SERIAL_FRAME_LINE_MAX];
    char frame_b[HAL_SERIAL_FRAME_LINE_MAX];
    char rx[HAL_SERIAL_FRAME_LINE_MAX * 2u + 8u];
    size_t len_a = 0u;
    size_t len_b = 0u;
    size_t used = 0u;
    uint16_t reply_seq = 0u;
    char reply_payload[192];

    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    TEST_ASSERT_TRUE(hal_serial_frame_encode(10u, "HELLO", frame_a, sizeof(frame_a), &len_a));
    TEST_ASSERT_TRUE(hal_serial_frame_encode(11u, "HELLO", frame_b, sizeof(frame_b), &len_b));

    memcpy(rx + used, frame_a, len_a);
    used += len_a;
    rx[used++] = '\r';
    rx[used++] = '\n';
    memcpy(rx + used, frame_b, len_b);
    used += len_b;
    rx[used++] = '\n';
    rx[used] = '\0';
    hal_mock_serial_inject_rx(rx, -1);

    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));
    TEST_ASSERT_EQUAL_UINT32(2u, s.hello_counter);
    TEST_ASSERT_EQUAL_UINT32(((uint32_t)2u << 20), hal_serial_session_id(&s));

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(11u, reply_seq);
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "OK HELLO"));
}

void test_poll_null_args_is_safe(void) {
    hal_serial_session_t s;
    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(1u, "HELLO", '\n');

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
    RUN_TEST(test_frame_crc8_matches_reference_vector);
    RUN_TEST(test_session_init_clears_state_and_binds_identity);
    RUN_TEST(test_session_init_nullable_identity_defaults_to_unknown);
    RUN_TEST(test_poll_hello_reports_full_identity_and_seq_echo);
    RUN_TEST(test_poll_hello_with_unknown_identity);
    RUN_TEST(test_poll_uses_injected_mock_uid);
    RUN_TEST(test_poll_unknown_payload_without_handler_returns_sc_unknown_cmd);
    RUN_TEST(test_poll_unknown_payload_with_handler_dispatches_inner_and_reply_seq);
    RUN_TEST(test_poll_drops_non_framed_and_bad_crc_lines);
    RUN_TEST(test_poll_mutes_debug_during_framed_dispatch_and_restores_state);
    RUN_TEST(test_hal_serial_session_println_is_gated_by_request_window);
    RUN_TEST(test_poll_handles_multiple_framed_commands_in_one_rx_buffer);
    RUN_TEST(test_poll_null_args_is_safe);
    return UNITY_END();
}

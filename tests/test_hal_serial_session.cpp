#include "utils/unity.h"
#include "hal/hal_serial_session.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

/* R1.6: the production HAL default vocabulary is empty (all NULL). The
 * test fixture below stays as the SC_* family so the existing assertions
 * (SC_OK AUTH_OK, SC_NOT_AUTHORIZED, SC_OK REBOOT, etc.) still apply.
 * Every test that exercises AUTH / REBOOT / unknown-cmd / not-ready paths
 * routes through `init_session_with_test_vocab` so the helper recognises
 * those tokens. HELLO-only tests can stick to `hal_serial_session_init`. */
static const hal_serial_session_vocabulary_t k_test_sc_vocab = {
    .cmd_auth_begin = "SC_AUTH_BEGIN",
    .cmd_auth_prove = "SC_AUTH_PROVE",
    .cmd_reboot_bootloader = "SC_REBOOT_BOOTLOADER",
    .reply_unknown_cmd = "SC_UNKNOWN_CMD",
    .reply_not_ready_hello_required = "SC_NOT_READY HELLO_REQUIRED",
    .reply_auth_challenge_fmt = "SC_OK AUTH_CHALLENGE %s",
    .reply_auth_ok = "SC_OK AUTH_OK",
    .reply_auth_failed_no_challenge = "SC_AUTH_FAILED no_challenge",
    .reply_auth_failed_bad_length = "SC_AUTH_FAILED bad_length",
    .reply_auth_failed_bad_hex = "SC_AUTH_FAILED bad_hex",
    .reply_auth_failed_key_derivation = "SC_AUTH_FAILED key_derivation",
    .reply_auth_failed_mac_compute = "SC_AUTH_FAILED mac_compute",
    .reply_auth_failed_bad_mac = "SC_AUTH_FAILED bad_mac",
    .reply_not_authorized = "SC_NOT_AUTHORIZED",
    .reply_reboot_ok = "SC_OK REBOOT",
};

static void init_session_with_test_vocab(hal_serial_session_t *s,
                                         const char *tag, const char *fw,
                                         const char *build) {
    hal_serial_session_init_with_vocabulary(s, tag, fw, build,
                                            &k_test_sc_vocab);
}

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

    init_session_with_test_vocab(&s, "ECU", "1.2.3", "abc123");

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
    init_session_with_test_vocab(&s, "ECU", NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("unknown", s.fw_version);
    TEST_ASSERT_EQUAL_STRING("unknown", s.build_id);

    init_session_with_test_vocab(&s, "ECU", "", "");
    TEST_ASSERT_EQUAL_STRING("unknown", s.fw_version);
    TEST_ASSERT_EQUAL_STRING("unknown", s.build_id);
}

void test_poll_hello_reports_full_identity_and_seq_echo(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[192];

    init_session_with_test_vocab(&s, "ECU", "1.2.3", "abc123");
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

    init_session_with_test_vocab(&s, "ECU", NULL, NULL);
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
    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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

/* ── Authentication handshake (Phase 3) ──────────────────────────────────── */

static const uint8_t k_mock_uid[HAL_DEVICE_UID_BYTES] = {
    /* Default mock UID: E6 61 A4 D1 23 45 67 AB. */
    0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
};

static void hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    for (size_t i = 0u; i < out_len; ++i) {
        unsigned int v = 0u;
        sscanf(hex + i * 2u, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void bytes_to_hex_lower(const uint8_t *bytes, size_t len,
                               char *out, size_t out_size) {
    static const char k_hex[] = "0123456789abcdef";
    if (len * 2u + 1u > out_size) {
        if (out_size > 0u) out[0] = '\0';
        return;
    }
    for (size_t i = 0u; i < len; ++i) {
        out[i * 2u] = k_hex[(bytes[i] >> 4) & 0x0Fu];
        out[i * 2u + 1u] = k_hex[bytes[i] & 0x0Fu];
    }
    out[len * 2u] = '\0';
}

/**
 * Read the framed reply payload, expect it to start with `SC_OK AUTH_CHALLENGE `
 * and decode the trailing 32-character hex challenge.
 */
static bool extract_challenge_from_reply(uint16_t expected_seq,
                                          uint8_t out_challenge[HAL_SC_AUTH_CHALLENGE_BYTES]) {
    uint16_t seq = 0u;
    char payload[192];
    if (!decode_last_framed_reply(&seq, payload, sizeof(payload))) {
        return false;
    }
    if (seq != expected_seq) {
        return false;
    }
    static const char k_prefix[] = "SC_OK AUTH_CHALLENGE ";
    if (strncmp(payload, k_prefix, sizeof(k_prefix) - 1u) != 0) {
        return false;
    }
    const char *hex = payload + sizeof(k_prefix) - 1u;
    if (strlen(hex) != HAL_SC_AUTH_CHALLENGE_BYTES * 2u) {
        return false;
    }
    hex_to_bytes(hex, out_challenge, HAL_SC_AUTH_CHALLENGE_BYTES);
    return true;
}

void test_auth_begin_before_hello_returns_not_ready(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(5u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(s.challenge_pending);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(5u, reply_seq);
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "SC_NOT_READY"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "HELLO_REQUIRED"));
}

void test_auth_begin_after_hello_issues_challenge(void) {
    hal_serial_session_t s;
    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));

    inject_framed_line(2u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(s.challenge_pending);
    TEST_ASSERT_TRUE(extract_challenge_from_reply(2u, challenge));
    /* Challenge must not be all-zero. */
    uint8_t accumulator = 0u;
    for (size_t i = 0u; i < HAL_SC_AUTH_CHALLENGE_BYTES; ++i) {
        accumulator = (uint8_t)(accumulator | challenge[i]);
    }
    TEST_ASSERT_NOT_EQUAL_UINT8(0u, accumulator);
}

void test_repeated_auth_begin_yields_different_challenges(void) {
    hal_serial_session_t s;
    uint8_t a[HAL_SC_AUTH_CHALLENGE_BYTES];
    uint8_t b[HAL_SC_AUTH_CHALLENGE_BYTES];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);

    /* Move the mock micros forward between AUTH_BEGINs so the SHA-256 mix
     * has different entropy in addition to the auth_counter increment. */
    inject_framed_line(2u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(extract_challenge_from_reply(2u, a));

    hal_mock_set_micros(123456u);
    inject_framed_line(3u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(extract_challenge_from_reply(3u, b));

    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, sizeof(a)));
}

void test_auth_prove_with_correct_mac_authenticates_session(void) {
    hal_serial_session_t s;
    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);

    inject_framed_line(2u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(extract_challenge_from_reply(2u, challenge));
    const uint32_t session_id = hal_serial_session_id(&s);
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, session_id);

    /* Compute the expected MAC against the bench-known mock UID. */
    uint8_t key[HAL_SC_AUTH_KEY_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_derive_device_key(k_mock_uid, sizeof(k_mock_uid), key));
    uint8_t mac[HAL_SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_compute_response(key, challenge, sizeof(challenge),
                                                  session_id, mac));

    char hex[HAL_SC_AUTH_RESPONSE_BYTES * 2u + 1u];
    bytes_to_hex_lower(mac, sizeof(mac), hex, sizeof(hex));

    char prove_line[256];
    snprintf(prove_line, sizeof(prove_line), "SC_AUTH_PROVE %s", hex);
    inject_framed_line(3u, prove_line, '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_serial_session_is_authenticated(&s));
    TEST_ASSERT_FALSE(s.challenge_pending);
    TEST_ASSERT_EQUAL_UINT32(0u, s.auth_failures);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(3u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_OK AUTH_OK", reply_payload);
}

void test_auth_prove_with_bad_mac_fails_and_consumes_challenge(void) {
    hal_serial_session_t s;
    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);

    inject_framed_line(2u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(extract_challenge_from_reply(2u, challenge));

    /* All-zero MAC will not match any real HMAC output. */
    char prove_line[256];
    snprintf(prove_line, sizeof(prove_line),
             "SC_AUTH_PROVE 0000000000000000000000000000000000000000000000000000000000000000");
    inject_framed_line(3u, prove_line, '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_serial_session_is_authenticated(&s));
    TEST_ASSERT_FALSE(s.challenge_pending);
    TEST_ASSERT_EQUAL_UINT32(1u, s.auth_failures);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(3u, reply_seq);
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "SC_AUTH_FAILED"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "bad_mac"));

    /* Replay of even the original AUTH_PROVE line is now refused - challenge
     * was consumed by the failed attempt. */
    inject_framed_line(4u, prove_line, '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(4u, reply_seq);
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "SC_AUTH_FAILED"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "no_challenge"));
}

void test_auth_prove_with_malformed_payload_is_rejected(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    inject_framed_line(2u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    /* Length too short. */
    inject_framed_line(3u, "SC_AUTH_PROVE deadbeef", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "SC_AUTH_FAILED"));
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "bad_length"));
    TEST_ASSERT_FALSE(hal_serial_session_is_authenticated(&s));
}

void test_new_hello_clears_authenticated_state(void) {
    hal_serial_session_t s;
    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);

    inject_framed_line(2u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(extract_challenge_from_reply(2u, challenge));

    uint8_t key[HAL_SC_AUTH_KEY_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_derive_device_key(k_mock_uid, sizeof(k_mock_uid), key));
    uint8_t mac[HAL_SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_compute_response(key, challenge, sizeof(challenge),
                                                  hal_serial_session_id(&s), mac));
    char hex[HAL_SC_AUTH_RESPONSE_BYTES * 2u + 1u];
    bytes_to_hex_lower(mac, sizeof(mac), hex, sizeof(hex));
    char prove_line[256];
    snprintf(prove_line, sizeof(prove_line), "SC_AUTH_PROVE %s", hex);
    inject_framed_line(3u, prove_line, '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(hal_serial_session_is_authenticated(&s));

    /* New HELLO: re-mints session_id and clears auth state. */
    hal_mock_set_millis(1u);  /* Force a different session id seed. */
    inject_framed_line(4u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_FALSE(hal_serial_session_is_authenticated(&s));
    TEST_ASSERT_FALSE(s.challenge_pending);
}

/* ── SC_REBOOT_BOOTLOADER (Phase 5) ──────────────────────────────────────── */

/**
 * Drive a session through HELLO + AUTH_BEGIN + AUTH_PROVE so the next
 * test step starts from a fully authenticated state. Returns the session
 * id for callers that need it.
 */
static uint32_t authenticate_session(hal_serial_session_t *s,
                                     uint16_t hello_seq,
                                     uint16_t begin_seq,
                                     uint16_t prove_seq) {
    inject_framed_line(hello_seq, "HELLO", '\n');
    hal_serial_session_poll(s);
    inject_framed_line(begin_seq, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(s);

    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    TEST_ASSERT_TRUE(extract_challenge_from_reply(begin_seq, challenge));
    const uint32_t session_id = hal_serial_session_id(s);

    uint8_t key[HAL_SC_AUTH_KEY_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_derive_device_key(k_mock_uid, sizeof(k_mock_uid), key));
    uint8_t mac[HAL_SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_compute_response(key, challenge, sizeof(challenge),
                                                  session_id, mac));
    char hex[HAL_SC_AUTH_RESPONSE_BYTES * 2u + 1u];
    bytes_to_hex_lower(mac, sizeof(mac), hex, sizeof(hex));

    char prove_line[256];
    snprintf(prove_line, sizeof(prove_line), "SC_AUTH_PROVE %s", hex);
    inject_framed_line(prove_seq, prove_line, '\n');
    hal_serial_session_poll(s);
    TEST_ASSERT_TRUE(hal_serial_session_is_authenticated(s));
    return session_id;
}

void test_reboot_bootloader_without_auth_is_rejected(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    hal_mock_bootloader_reset_flag();

    /* No HELLO and no AUTH yet - must refuse. */
    inject_framed_line(5u, "SC_REBOOT_BOOTLOADER", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_mock_bootloader_was_requested());
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(5u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_NOT_AUTHORIZED", reply_payload);
}

void test_reboot_bootloader_after_hello_only_is_rejected(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    hal_mock_bootloader_reset_flag();

    /* HELLO activates the session but the AUTH path has not been
     * cleared - the bootloader command must still be refused. */
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));
    TEST_ASSERT_FALSE(hal_serial_session_is_authenticated(&s));

    inject_framed_line(2u, "SC_REBOOT_BOOTLOADER", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_mock_bootloader_was_requested());
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(2u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_NOT_AUTHORIZED", reply_payload);
}

void test_reboot_bootloader_after_auth_acks_and_enters_bootloader(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    hal_mock_bootloader_reset_flag();
    (void)authenticate_session(&s, 1u, 2u, 3u);

    inject_framed_line(7u, "SC_REBOOT_BOOTLOADER", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_mock_bootloader_was_requested());
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(7u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_OK REBOOT", reply_payload);
}

void test_reboot_bootloader_blocked_after_new_hello_clears_auth(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
    hal_mock_bootloader_reset_flag();
    (void)authenticate_session(&s, 1u, 2u, 3u);

    /* New HELLO mints a new session id and clears authenticated state.
     * A subsequent SC_REBOOT_BOOTLOADER must be refused. */
    hal_mock_set_millis(1u);
    inject_framed_line(4u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_FALSE(hal_serial_session_is_authenticated(&s));

    inject_framed_line(5u, "SC_REBOOT_BOOTLOADER", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_mock_bootloader_was_requested());
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload, sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(5u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_NOT_AUTHORIZED", reply_payload);
}

void test_poll_null_args_is_safe(void) {
    hal_serial_session_t s;
    init_session_with_test_vocab(&s, "ECU", "1.0.0", "dev");
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
    RUN_TEST(test_auth_begin_before_hello_returns_not_ready);
    RUN_TEST(test_auth_begin_after_hello_issues_challenge);
    RUN_TEST(test_repeated_auth_begin_yields_different_challenges);
    RUN_TEST(test_auth_prove_with_correct_mac_authenticates_session);
    RUN_TEST(test_auth_prove_with_bad_mac_fails_and_consumes_challenge);
    RUN_TEST(test_auth_prove_with_malformed_payload_is_rejected);
    RUN_TEST(test_new_hello_clears_authenticated_state);
    RUN_TEST(test_reboot_bootloader_without_auth_is_rejected);
    RUN_TEST(test_reboot_bootloader_after_hello_only_is_rejected);
    RUN_TEST(test_reboot_bootloader_after_auth_acks_and_enters_bootloader);
    RUN_TEST(test_reboot_bootloader_blocked_after_new_hello_clears_auth);
    RUN_TEST(test_poll_null_args_is_safe);
    return UNITY_END();
}

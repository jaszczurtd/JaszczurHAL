#include "utils/unity.h"
#include "hal/hal_serial_session.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

/* ── Test framing helpers (copied from test_hal_serial_session.cpp) ─────── */

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

/* Mock UID matches the default mock device UID set by hal_mock_reset_device_uid. */
static const uint8_t k_mock_uid[HAL_DEVICE_UID_BYTES] = {
    0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
};

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

static void hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    for (size_t i = 0u; i < out_len; ++i) {
        unsigned int v = 0u;
        sscanf(hex + i * 2u, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

/* ── Custom vocabulary used by the override tests ───────────────────────── */

/* Same prefix / suffix layout as the default but with project-specific
 * tokens. The test asserts that wire output and command matching follow
 * these strings, not the SC_* defaults. */
static const hal_serial_session_vocabulary_t k_custom_vocab = {
    .cmd_auth_begin = "X_AUTH_BEGIN",
    .cmd_auth_prove = "X_AUTH_PROVE",
    .cmd_reboot_bootloader = "X_REBOOT",
    .reply_unknown_cmd = "X_UNKNOWN",
    .reply_not_ready_hello_required = "X_NOT_READY HELLO_REQUIRED",
    .reply_auth_challenge_fmt = "X_OK AUTH_CHALLENGE %s",
    .reply_auth_ok = "X_OK AUTH_OK",
    .reply_auth_failed_no_challenge = "X_AUTH_FAILED no_challenge",
    .reply_auth_failed_bad_length = "X_AUTH_FAILED bad_length",
    .reply_auth_failed_bad_hex = "X_AUTH_FAILED bad_hex",
    .reply_auth_failed_key_derivation = "X_AUTH_FAILED key_derivation",
    .reply_auth_failed_mac_compute = "X_AUTH_FAILED mac_compute",
    .reply_auth_failed_bad_mac = "X_AUTH_FAILED bad_mac",
    .reply_not_authorized = "X_NOT_AUTHORIZED",
    .reply_reboot_ok = "X_OK REBOOT",
};

/* Partial override: only the "unknown" reply is custom, all auth tokens
 * keep the SC_* defaults via NULL fallback. */
static const hal_serial_session_vocabulary_t k_partial_vocab = {
    /* cmd_* and reply_auth_* fields are zero-initialised → NULL → default. */
    .reply_unknown_cmd = "Y_UNKNOWN",
};

void setUp(void) {
    hal_debug_init(115200);
    hal_debug_set_muted(false);
    hal_mock_serial_reset();
    hal_mock_set_millis(0);
    hal_mock_reset_device_uid();
}

void tearDown(void) {}

/* ── Default-vocabulary smoke (no custom override) ──────────────────────── */

void test_classic_init_keeps_default_vocabulary(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    hal_serial_session_init(&s, "ECU", "1.0.0", "dev");
    TEST_ASSERT_NULL(s.vocab);

    /* Unknown command → default SC_UNKNOWN_CMD reply. */
    inject_framed_line(9u, "PING", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(9u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", reply_payload);
}

/* ── Full custom vocabulary: every override visible on the wire ─────────── */

void test_custom_vocab_renames_unknown_reply(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_custom_vocab);
    TEST_ASSERT_EQUAL_PTR(&k_custom_vocab, s.vocab);

    inject_framed_line(1u, "PING", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(1u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("X_UNKNOWN", reply_payload);
}

void test_custom_vocab_renames_auth_command_and_replies(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[192];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_custom_vocab);

    /* Activate the session via the structural HELLO (intentionally NOT
     * configurable). */
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(hal_serial_session_is_active(&s));

    /* Renamed AUTH_BEGIN → custom challenge prefix on wire. */
    inject_framed_line(2u, "X_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(s.challenge_pending);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(2u, reply_seq);

    static const char k_prefix[] = "X_OK AUTH_CHALLENGE ";
    TEST_ASSERT_EQUAL_INT(0, strncmp(reply_payload, k_prefix,
                                      sizeof(k_prefix) - 1u));

    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    hex_to_bytes(reply_payload + sizeof(k_prefix) - 1u, challenge,
                 sizeof(challenge));

    /* Compute valid response and submit via renamed prove command. */
    uint8_t key[HAL_SC_AUTH_KEY_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_derive_device_key(k_mock_uid,
                                                   sizeof(k_mock_uid), key));
    uint8_t mac[HAL_SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_compute_response(key, challenge,
                                                  sizeof(challenge),
                                                  hal_serial_session_id(&s),
                                                  mac));
    char hex[HAL_SC_AUTH_RESPONSE_BYTES * 2u + 1u];
    bytes_to_hex_lower(mac, sizeof(mac), hex, sizeof(hex));

    char prove_line[256];
    snprintf(prove_line, sizeof(prove_line), "X_AUTH_PROVE %s", hex);
    inject_framed_line(3u, prove_line, '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_serial_session_is_authenticated(&s));
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(3u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("X_OK AUTH_OK", reply_payload);
}

void test_custom_vocab_sc_prefix_falls_through_to_unknown(void) {
    /* When commands are renamed, the original SC_AUTH_BEGIN literal must
     * NOT be intercepted by the helper — it should reach the unknown
     * handler (or the default unknown-reply path) untouched. */
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_custom_vocab);
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);

    inject_framed_line(2u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(s.challenge_pending);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(2u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("X_UNKNOWN", reply_payload);
}

void test_custom_vocab_reboot_emits_renamed_reply(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_custom_vocab);
    hal_mock_bootloader_reset_flag();

    /* Authenticate via the renamed handshake. */
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    inject_framed_line(2u, "X_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    static const char k_prefix[] = "X_OK AUTH_CHALLENGE ";
    char challenge_payload[192];
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, challenge_payload,
                                              sizeof(challenge_payload)));
    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    hex_to_bytes(challenge_payload + sizeof(k_prefix) - 1u, challenge,
                 sizeof(challenge));

    uint8_t key[HAL_SC_AUTH_KEY_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_derive_device_key(k_mock_uid,
                                                   sizeof(k_mock_uid), key));
    uint8_t mac[HAL_SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_compute_response(key, challenge,
                                                  sizeof(challenge),
                                                  hal_serial_session_id(&s),
                                                  mac));
    char hex[HAL_SC_AUTH_RESPONSE_BYTES * 2u + 1u];
    bytes_to_hex_lower(mac, sizeof(mac), hex, sizeof(hex));
    char prove_line[256];
    snprintf(prove_line, sizeof(prove_line), "X_AUTH_PROVE %s", hex);
    inject_framed_line(3u, prove_line, '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(hal_serial_session_is_authenticated(&s));

    /* Reboot via renamed command, expect renamed ACK + bootloader entry. */
    inject_framed_line(4u, "X_REBOOT", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(hal_mock_bootloader_was_requested());
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(4u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("X_OK REBOOT", reply_payload);
}

void test_custom_vocab_not_authorized_uses_override(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_custom_vocab);
    hal_mock_bootloader_reset_flag();

    /* Reboot before AUTH → must refuse with the renamed token. */
    inject_framed_line(1u, "X_REBOOT", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(hal_mock_bootloader_was_requested());
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(1u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("X_NOT_AUTHORIZED", reply_payload);
}

void test_custom_vocab_not_ready_uses_override(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_custom_vocab);

    /* AUTH_BEGIN before HELLO — must use the renamed not-ready string. */
    inject_framed_line(1u, "X_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_FALSE(s.challenge_pending);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(1u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("X_NOT_READY HELLO_REQUIRED", reply_payload);
}

void test_custom_vocab_auth_failed_uses_override(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[128];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_custom_vocab);
    inject_framed_line(1u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    inject_framed_line(2u, "X_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    /* deliberately wrong length to trigger reply_auth_failed_bad_length. */
    inject_framed_line(3u, "X_AUTH_PROVE deadbeef", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(3u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("X_AUTH_FAILED bad_length", reply_payload);
}

/* ── Partial override: only one field set, rest fall back ───────────────── */

void test_partial_vocab_falls_back_to_defaults_per_field(void) {
    hal_serial_session_t s;
    uint16_t reply_seq = 0u;
    char reply_payload[192];

    hal_serial_session_init_with_vocabulary(&s, "ECU", "1.0.0", "dev",
                                            &k_partial_vocab);

    /* Unknown command → custom Y_UNKNOWN. */
    inject_framed_line(1u, "PING", '\n');
    hal_serial_session_poll(&s);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(1u, reply_seq);
    TEST_ASSERT_EQUAL_STRING("Y_UNKNOWN", reply_payload);

    /* AUTH commands NOT overridden → still respond to default SC_AUTH_BEGIN
     * literal and default SC_OK AUTH_CHALLENGE prefix. */
    inject_framed_line(2u, "HELLO", '\n');
    hal_serial_session_poll(&s);
    inject_framed_line(3u, "SC_AUTH_BEGIN", '\n');
    hal_serial_session_poll(&s);

    TEST_ASSERT_TRUE(s.challenge_pending);
    TEST_ASSERT_TRUE(decode_last_framed_reply(&reply_seq, reply_payload,
                                              sizeof(reply_payload)));
    TEST_ASSERT_EQUAL_UINT16(3u, reply_seq);
    TEST_ASSERT_NOT_NULL(strstr(reply_payload, "SC_OK AUTH_CHALLENGE "));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_classic_init_keeps_default_vocabulary);
    RUN_TEST(test_custom_vocab_renames_unknown_reply);
    RUN_TEST(test_custom_vocab_renames_auth_command_and_replies);
    RUN_TEST(test_custom_vocab_sc_prefix_falls_through_to_unknown);
    RUN_TEST(test_custom_vocab_reboot_emits_renamed_reply);
    RUN_TEST(test_custom_vocab_not_authorized_uses_override);
    RUN_TEST(test_custom_vocab_not_ready_uses_override);
    RUN_TEST(test_custom_vocab_auth_failed_uses_override);
    RUN_TEST(test_partial_vocab_falls_back_to_defaults_per_field);
    return UNITY_END();
}

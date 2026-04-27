#pragma once

/**
 * @file hal_serial_session.h
 * @brief Framed serial session helper for the SerialConfigurator transport.
 *
 * This helper provides a tiny session state machine that speaks ONLY the
 * framed wire protocol described in @ref hal_serial_frame.h:
 *
 *     $SC,<seq>,<inner>*<crc8>\n
 *
 * Lines that do not start with the `$SC,` sentinel are silently discarded.
 * There is intentionally no plain-text fall-through: the host SerialConfigurator
 * always frames its requests, and removing the legacy path eliminates a class
 * of bugs (substring matches in debug logs, unframed bytes corrupting the
 * stream, modules accidentally responding to noise).
 *
 * Built-in command:
 * - `HELLO` → `OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>`
 *
 * Any other inner payload is forwarded to the user-supplied unknown-line
 * handler (see @ref hal_serial_session_set_unknown_handler) so each module
 * can implement its own SC_* command set.
 *
 * Identity fields (module tag, firmware version, build id) are immutable for
 * the lifetime of a session and are therefore captured at init time rather
 * than passed on every poll. The device UID is sourced from
 * @ref hal_get_device_uid_hex and is also captured at init.
 */

#include "hal_config.h"
#include "hal_serial.h"
#include "hal_serial_frame.h"
#include "hal_serial_session_vocabulary.h"
#include "hal_system.h"
#ifdef HAL_ENABLE_CRYPTO
#include "hal_sc_auth.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Current session protocol version. */
#define HAL_SERIAL_SESSION_PROTOCOL_VERSION 1u

/**
 * @brief Resolve a vocabulary field for the given session.
 *
 * Shorthand for @ref HAL_SERIAL_SESSION_VOCAB_FIELD that takes the session
 * pointer directly. Falls back to @ref hal_serial_session_vocabulary_default
 * when either the session has no vocab override (classic init) or the
 * specific field is NULL in the override.
 */
#define HAL_SERIAL_SESSION_VOCAB(session, field) \
    HAL_SERIAL_SESSION_VOCAB_FIELD((session)->vocab, field)

/** @brief Maximum accepted command line length (excluding terminator).
 *
 *  Sized to fit a framed `$SC,<seq>,<inner>*<crc>` request whose @c <inner>
 *  may carry the longest configurator command (currently `SC_GET_PARAM
 *  <id>`), with margin for future growth. */
#define HAL_SERIAL_SESSION_MAX_LINE 128u

/** @brief Fallback string for fw/build fields when the caller passes NULL/""  */
#define HAL_SERIAL_SESSION_UNKNOWN "unknown"

/**
 * @brief Optional callback invoked when an incoming inner payload is not
 *        recognised as a built-in session command.
 *
 * When registered via @ref hal_serial_session_set_unknown_handler, the helper
 * forwards the inner payload (NUL-terminated, already unwrapped from the
 * `$SC,<seq>,...*<crc>` envelope) to this callback. This lets each module
 * implement its own SC_* command set without duplicating frame parsing.
 *
 * The callback is responsible for any reply it wishes to send via
 * @ref hal_serial_session_println; replies are automatically wrapped into a
 * frame carrying the same @c <seq> as the inbound request, so the host can
 * correlate them.
 *
 * If no handler is registered, the helper emits a `SC_UNKNOWN_CMD` reply.
 */
typedef void (*hal_serial_session_unknown_cb_t)(const char *line, void *user);

/**
 * @brief Runtime state for one serial session endpoint.
 *
 * The string pointers @c module_tag, @c fw_version and @c build_id are stored
 * as-is and must remain valid for the lifetime of the session (typically
 * compile-time string literals defined in each module's config header).
 */
typedef struct {
    bool active;                               /**< true after successful HELLO handshake. */
    uint32_t session_id;                       /**< Last assigned session id. */
    uint32_t hello_counter;                    /**< Monotonic counter of accepted HELLO commands. */
    uint32_t last_activity_ms;                 /**< Last time a command was processed. */
    uint8_t line_len;                          /**< Current receive line length. */
    char line[HAL_SERIAL_SESSION_MAX_LINE + 1u]; /**< Command line buffer (NUL-terminated on parse). */
    const char *module_tag;                    /**< Module identity (MODULE_NAME) used in HELLO. */
    const char *fw_version;                    /**< Firmware version string used in HELLO. */
    const char *build_id;                      /**< Build identifier string used in HELLO. */
    char uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE]; /**< Cached device UID hex string. */
    hal_serial_session_unknown_cb_t unknown_handler; /**< Optional sink for non-protocol command lines. */
    void *unknown_user;                        /**< Opaque user pointer forwarded to @c unknown_handler. */
    bool in_request;                           /**< true while dispatching a framed request (gates `hal_serial_session_println`). */
    uint16_t request_seq;                      /**< Sequence number of the in-flight framed request. */
    const hal_serial_session_vocabulary_t *vocab; /**< Optional override of inbound/outbound SC tokens (NULL → defaults). */
#ifdef HAL_ENABLE_CRYPTO
    /* Authentication state (Phase 3). Compiled in only when
     * HAL_ENABLE_CRYPTO is defined; otherwise the session helper still
     * accepts framed traffic but never enters the authenticated state. */
    uint8_t uid_bytes[HAL_DEVICE_UID_BYTES];   /**< Cached binary device UID (used for K_device). */
    bool authenticated;                        /**< true once a valid AUTH_PROVE has been seen for the current session. */
    bool challenge_pending;                    /**< true between AUTH_BEGIN and the next AUTH_PROVE (or session reset). */
    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES]; /**< Last issued challenge nonce. */
    uint32_t auth_counter;                     /**< Monotonic counter mixed into challenge derivation. */
    uint32_t auth_failures;                    /**< Counter of failed AUTH_PROVE attempts (for Phase 7 lockout). */
#endif /* HAL_ENABLE_CRYPTO */
} hal_serial_session_t;

/**
 * @brief Initialize/clear one serial session context, bind identity fields,
 *        and (optionally) install a custom @ref hal_serial_session_vocabulary_t.
 *
 * The identity fields are captured by pointer and the device UID hex string
 * is captured by value at init time. @p module_tag must remain valid for the
 * lifetime of the session; @p fw_version and @p build_id may be NULL and will
 * be reported as @ref HAL_SERIAL_SESSION_UNKNOWN.
 *
 * @p vocab is borrowed and must remain valid for the lifetime of the session.
 * Pass NULL to keep the historical Fiesta-default tokens (equivalent to
 * @ref hal_serial_session_init).
 *
 * @param session     Session context to reset.
 * @param module_tag  Short module identifier (e.g. "ECU"), must not be NULL.
 * @param fw_version  Firmware version string, or NULL.
 * @param build_id    Build identifier string, or NULL.
 * @param vocab       Optional vocabulary override, or NULL for defaults.
 */
static inline void hal_serial_session_init_with_vocabulary(
    hal_serial_session_t *session,
    const char *module_tag,
    const char *fw_version,
    const char *build_id,
    const hal_serial_session_vocabulary_t *vocab) {
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
    session->module_tag = (module_tag != NULL) ? module_tag : HAL_SERIAL_SESSION_UNKNOWN;
    session->fw_version = ((fw_version != NULL) && (fw_version[0] != '\0'))
                              ? fw_version : HAL_SERIAL_SESSION_UNKNOWN;
    session->build_id   = ((build_id != NULL) && (build_id[0] != '\0'))
                              ? build_id : HAL_SERIAL_SESSION_UNKNOWN;
#ifdef HAL_ENABLE_CRYPTO
    hal_get_device_uid(session->uid_bytes);
#endif
    if (!hal_get_device_uid_hex(session->uid_hex, sizeof(session->uid_hex))) {
        session->uid_hex[0] = '\0';
    }
    session->unknown_handler = NULL;
    session->unknown_user = NULL;
    session->vocab = vocab;
}

/**
 * @brief Initialize a serial session with the historical default vocabulary.
 *
 * Thin wrapper over @ref hal_serial_session_init_with_vocabulary that passes
 * NULL for @c vocab. Kept as the canonical entry point for callers that do
 * not need to override SC token strings.
 */
static inline void hal_serial_session_init(hal_serial_session_t *session,
                                           const char *module_tag,
                                           const char *fw_version,
                                           const char *build_id) {
    hal_serial_session_init_with_vocabulary(session, module_tag, fw_version,
                                            build_id, NULL);
}

/**
 * @brief Register (or clear) a sink for unrecognised command lines.
 *
 * When @p cb is non-NULL the session helper stops printing the default
 * `ERR UNKNOWN` reply for unknown lines and instead invokes @p cb with the
 * received line. Pass NULL for @p cb to restore the default behaviour.
 *
 * @param session Session context.
 * @param cb      Callback to invoke for unknown lines, or NULL to clear.
 * @param user    Opaque pointer forwarded to @p cb.
 */
static inline void hal_serial_session_set_unknown_handler(hal_serial_session_t *session,
                                                          hal_serial_session_unknown_cb_t cb,
                                                          void *user) {
    if (session == NULL) {
        return;
    }
    session->unknown_handler = cb;
    session->unknown_user = user;
}

/**
 * @brief Return true when session is currently active.
 * @param session Session context.
 * @return true if active, otherwise false.
 */
static inline bool hal_serial_session_is_active(const hal_serial_session_t *session) {
    return (session != NULL) ? session->active : false;
}

/**
 * @brief Return last assigned session id.
 * @param session Session context.
 * @return Session id, or 0 when session is NULL or never initialized by HELLO.
 */
static inline uint32_t hal_serial_session_id(const hal_serial_session_t *session) {
    return (session != NULL) ? session->session_id : 0u;
}

/**
 * @brief Return true when a valid AUTH_PROVE has been seen for the current
 *        session (i.e. the session is authenticated and may execute
 *        sensitive commands).
 *
 * Cleared by every fresh HELLO and by AUTH_PROVE failure.
 *
 * Always returns false when @c HAL_ENABLE_CRYPTO is not defined — the
 * AUTH handshake is then compiled out and the session can never enter
 * the authenticated state.
 *
 * @param session Session context.
 */
static inline bool hal_serial_session_is_authenticated(
    const hal_serial_session_t *session) {
#ifdef HAL_ENABLE_CRYPTO
    return (session != NULL) ? session->authenticated : false;
#else
    (void)session;
    return false;
#endif
}

/**
 * @brief Return true when a line should suppress debug logs during handling.
 *
 * Only framed protocol lines (`$SC,...`) qualify; everything else is dropped
 * by the parser anyway, so leaving debug enabled for those bytes is harmless.
 */
static inline bool hal_serial_session_should_mute_debug_for_line(const char *line) {
    if (line == NULL) {
        return false;
    }
    return strncmp(line, HAL_SERIAL_FRAME_PREFIX, HAL_SERIAL_FRAME_PREFIX_LEN) == 0;
}

/**
 * @brief Emit one SC payload as a framed reply to the in-flight request.
 *
 * Must be called from inside @ref hal_serial_session_poll while a request is
 * being dispatched (i.e. from the HELLO branch or from the user-supplied
 * unknown-line handler). Calls outside that window are dropped: the framed
 * protocol has no place for unsolicited responses, and emitting raw bytes
 * would corrupt the stream.
 *
 * @param session Session context (must be non-NULL).
 * @param payload Inner payload to wrap. Must not contain `*`, `\r` or `\n`.
 */
static inline void hal_serial_session_println(hal_serial_session_t *session,
                                              const char *payload) {
    if ((session == NULL) || (payload == NULL) || !session->in_request) {
        return;
    }

    char framed[HAL_SERIAL_FRAME_LINE_MAX];
    if (hal_serial_frame_encode(session->request_seq, payload,
                                framed, sizeof(framed), NULL)) {
        hal_serial_println(framed);
    }
}

/**
 * @brief Internal: build and emit the OK HELLO response.
 */
static inline void hal_serial_session__emit_hello(hal_serial_session_t *session) {
    char response[192];
    snprintf(response, sizeof(response),
             "OK HELLO module=%s proto=%u session=%lu fw=%s build=%s uid=%s",
             session->module_tag,
             (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
             (unsigned long)session->session_id,
             (session->fw_version != NULL) ? session->fw_version
                                           : HAL_SERIAL_SESSION_UNKNOWN,
             (session->build_id != NULL) ? session->build_id
                                         : HAL_SERIAL_SESSION_UNKNOWN,
             session->uid_hex);
    hal_serial_session_println(session, response);
}

#ifdef HAL_ENABLE_CRYPTO
/**
 * @brief Internal: clear authentication state.
 *
 * Called on every fresh HELLO (which mints a new session_id) and after a
 * failed AUTH_PROVE.
 */
static inline void hal_serial_session__reset_auth(hal_serial_session_t *session) {
    session->authenticated = false;
    session->challenge_pending = false;
    memset(session->challenge, 0, sizeof(session->challenge));
}

/**
 * @brief Internal: parse exactly 2*N lowercase/uppercase hex characters into N bytes.
 * @return true on success.
 */
static inline bool hal_serial_session__hex_decode(const char *hex,
                                                  uint8_t *out,
                                                  size_t out_len) {
    if (hex == NULL || out == NULL) {
        return false;
    }
    for (size_t i = 0u; i < out_len; ++i) {
        uint8_t v = 0u;
        for (uint8_t k = 0u; k < 2u; ++k) {
            const char c = hex[i * 2u + k];
            uint8_t nibble;
            if (c >= '0' && c <= '9') {
                nibble = (uint8_t)(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                nibble = (uint8_t)(10 + (c - 'A'));
            } else if (c >= 'a' && c <= 'f') {
                nibble = (uint8_t)(10 + (c - 'a'));
            } else {
                return false;
            }
            v = (uint8_t)((v << 4) | nibble);
        }
        out[i] = v;
    }
    return true;
}

/**
 * @brief Internal: derive a fresh challenge nonce.
 *
 * The challenge is produced as the first @ref HAL_SC_AUTH_CHALLENGE_BYTES of
 *
 *     SHA-256(uid_bytes || micros64_be || session_id_be || hello_counter_be
 *             || auth_counter_be)
 *
 * which is unpredictable enough to withstand the bench-laptop replay threat
 * model from §7.3 of the SerialConfigurator context provider. RP2040 bench
 * hardware exposes a microsecond timer that ticks over millions of times per
 * second, so feeding it into SHA-256 yields fresh values per call without
 * relying on a hardware RNG that the HAL does not currently abstract.
 */
static inline void hal_serial_session__generate_challenge(
    hal_serial_session_t *session) {
    session->auth_counter++;

    uint8_t mix[HAL_DEVICE_UID_BYTES + 8u + 4u + 4u + 4u];
    size_t off = 0u;
    memcpy(&mix[off], session->uid_bytes, HAL_DEVICE_UID_BYTES);
    off += HAL_DEVICE_UID_BYTES;

    const uint64_t now = hal_micros64();
    for (size_t i = 0u; i < 8u; ++i) {
        mix[off + i] = (uint8_t)(now >> (56u - i * 8u));
    }
    off += 8u;

    hal_u32_to_bytes_be(session->session_id, &mix[off]);
    off += 4u;
    hal_u32_to_bytes_be(session->hello_counter, &mix[off]);
    off += 4u;
    hal_u32_to_bytes_be(session->auth_counter, &mix[off]);
    off += 4u;

    uint8_t digest[HAL_SHA256_DIGEST_BYTES];
    if (hal_sha256(mix, off, digest)) {
        memcpy(session->challenge, digest, HAL_SC_AUTH_CHALLENGE_BYTES);
    } else {
        /* Fallback: derive from session id + counter only — still unique
         * per call within a session. */
        memset(session->challenge, 0, HAL_SC_AUTH_CHALLENGE_BYTES);
        hal_u32_to_bytes_be(session->session_id, &session->challenge[0]);
        hal_u32_to_bytes_be(session->auth_counter, &session->challenge[4]);
        hal_u32_to_bytes_be(session->hello_counter, &session->challenge[8]);
    }
}

/**
 * @brief Internal: handle SC_AUTH_BEGIN.
 *
 * Requires an active (HELLO-acknowledged) session. Generates a fresh
 * challenge, stashes it on the session, and replies with the hex-encoded
 * challenge. Each AUTH_BEGIN supersedes any previously pending challenge.
 */
static inline void hal_serial_session__handle_auth_begin(
    hal_serial_session_t *session) {
    if (!session->active) {
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_not_ready_hello_required));
        return;
    }

    hal_serial_session__generate_challenge(session);
    session->challenge_pending = true;

    char hex[HAL_SC_AUTH_CHALLENGE_HEX_BUF_SIZE];
    static const char k_hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < HAL_SC_AUTH_CHALLENGE_BYTES; ++i) {
        hex[i * 2u] = k_hex[(session->challenge[i] >> 4) & 0x0Fu];
        hex[i * 2u + 1u] = k_hex[session->challenge[i] & 0x0Fu];
    }
    hex[HAL_SC_AUTH_CHALLENGE_BYTES * 2u] = '\0';

    char response[64];
    snprintf(response, sizeof(response),
             HAL_SERIAL_SESSION_VOCAB(session, reply_auth_challenge_fmt), hex);
    hal_serial_session_println(session, response);
}

/**
 * @brief Internal: handle SC_AUTH_PROVE <hex>.
 *
 * @p args points to the first character after `SC_AUTH_PROVE` (including the
 * leading space, which is skipped). On success the session becomes
 * authenticated; on failure the challenge is consumed (one-shot — the host
 * must request a new AUTH_BEGIN) and `auth_failures` is incremented.
 */
static inline void hal_serial_session__handle_auth_prove(
    hal_serial_session_t *session,
    const char *args) {
    if (!session->active) {
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_not_ready_hello_required));
        return;
    }
    if (!session->challenge_pending) {
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_no_challenge));
        return;
    }

    /* Skip leading spaces. */
    while (*args == ' ') {
        ++args;
    }

    /* Expect exactly 64 lowercase/uppercase hex chars (32 bytes), then
     * optional trailing whitespace, then end-of-string. */
    size_t hex_len = 0u;
    while (args[hex_len] != '\0' && args[hex_len] != ' ') {
        ++hex_len;
    }
    if (hex_len != HAL_SC_AUTH_RESPONSE_BYTES * 2u) {
        session->challenge_pending = false;
        session->auth_failures++;
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_bad_length));
        return;
    }

    uint8_t provided[HAL_SC_AUTH_RESPONSE_BYTES];
    if (!hal_serial_session__hex_decode(args, provided, sizeof(provided))) {
        session->challenge_pending = false;
        session->auth_failures++;
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_bad_hex));
        return;
    }

    uint8_t key[HAL_SC_AUTH_KEY_BYTES];
    if (!hal_sc_auth_derive_device_key(session->uid_bytes,
                                       HAL_DEVICE_UID_BYTES,
                                       key)) {
        session->challenge_pending = false;
        session->auth_failures++;
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_key_derivation));
        return;
    }

    uint8_t expected[HAL_SC_AUTH_RESPONSE_BYTES];
    if (!hal_sc_auth_compute_response(key,
                                      session->challenge,
                                      HAL_SC_AUTH_CHALLENGE_BYTES,
                                      session->session_id,
                                      expected)) {
        session->challenge_pending = false;
        session->auth_failures++;
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_mac_compute));
        return;
    }

    /* One-shot: consume the challenge regardless of outcome to defeat
     * brute-force-by-replay attempts within a single AUTH_BEGIN. */
    session->challenge_pending = false;

    if (!hal_sc_auth_macs_equal(provided, expected, sizeof(expected))) {
        session->auth_failures++;
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_bad_mac));
        return;
    }

    session->authenticated = true;
    hal_serial_session_println(session,
        HAL_SERIAL_SESSION_VOCAB(session, reply_auth_ok));
}

/**
 * @brief Internal: handle SC_REBOOT_BOOTLOADER (Phase 5).
 *
 * Auth-gated. The single guard is @c hal_serial_session_is_authenticated;
 * there is no RPM == 0 check or service-mode flag (per SC design defaults).
 *
 * On a successful handshake the function:
 *   1. emits the framed `SC_OK REBOOT` reply,
 *   2. sleeps briefly so the USB CDC stack has a chance to drain the ACK
 *      frame before the boot ROM grabs the controller,
 *   3. calls @c hal_enter_bootloader. On real hardware the call does not
 *      return; the mock backend simply flips a test-observable flag and
 *      returns, which lets host-side tests verify the request reached
 *      the right entry point.
 *
 * On an unauthenticated session the firmware refuses the command and
 * keeps running normally.
 */
static inline void hal_serial_session__handle_reboot_bootloader(
    hal_serial_session_t *session) {
    if (!session->authenticated) {
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_not_authorized));
        return;
    }
    hal_serial_session_println(session,
        HAL_SERIAL_SESSION_VOCAB(session, reply_reboot_ok));
    /* Drain window for the ACK frame on the USB CDC stack. */
    hal_delay_ms(50u);
    hal_enter_bootloader();
}
#endif /* HAL_ENABLE_CRYPTO */

/**
 * @brief Internal: dispatch one already-unwrapped inner command line.
 */
static inline void hal_serial_session__dispatch_inner(hal_serial_session_t *session,
                                                      const char *inner) {
    if (strcmp(inner, "HELLO") == 0) {
        session->active = true;
        session->hello_counter++;
        session->last_activity_ms = hal_millis();
        /* Non-cryptographic seed for bootstrap session tracking. */
        session->session_id =
            ((uint32_t)session->hello_counter << 20) ^
            (session->last_activity_ms & 0x000FFFFFu);
#ifdef HAL_ENABLE_CRYPTO
        /* New session id invalidates any previously authenticated state. */
        hal_serial_session__reset_auth(session);
#endif
        hal_serial_session__emit_hello(session);
        return;
    }

#ifdef HAL_ENABLE_CRYPTO
    const char *cmd_auth_begin =
        HAL_SERIAL_SESSION_VOCAB(session, cmd_auth_begin);
    if (strcmp(inner, cmd_auth_begin) == 0) {
        hal_serial_session__handle_auth_begin(session);
        return;
    }

    const char *cmd_auth_prove =
        HAL_SERIAL_SESSION_VOCAB(session, cmd_auth_prove);
    const size_t cmd_auth_prove_len = strlen(cmd_auth_prove);
    if (strncmp(inner, cmd_auth_prove, cmd_auth_prove_len) == 0 &&
        (inner[cmd_auth_prove_len] == ' ' ||
         inner[cmd_auth_prove_len] == '\0')) {
        hal_serial_session__handle_auth_prove(
            session, inner + cmd_auth_prove_len);
        return;
    }

    const char *cmd_reboot_bootloader =
        HAL_SERIAL_SESSION_VOCAB(session, cmd_reboot_bootloader);
    if (strcmp(inner, cmd_reboot_bootloader) == 0) {
        hal_serial_session__handle_reboot_bootloader(session);
        return;
    }
#endif /* HAL_ENABLE_CRYPTO */

    if (session->unknown_handler != NULL) {
        session->unknown_handler(inner, session->unknown_user);
    } else {
        hal_serial_session_println(session,
            HAL_SERIAL_SESSION_VOCAB(session, reply_unknown_cmd));
    }
}

/**
 * @brief Process all pending serial bytes and handle supported commands.
 *
 * The parser is line-oriented (`\n` and/or `\r` terminate the command).
 * Only framed lines (`$SC,<seq>,<inner>*<crc8>`) are accepted; everything
 * else is silently discarded, including:
 *   - lines that do not start with the `$SC,` sentinel,
 *   - frames whose CRC does not validate,
 *   - frames whose seq is malformed or out of range.
 *
 * Replies emitted by built-in commands or by the unknown-line handler are
 * automatically wrapped in a frame carrying the same @c <seq> as the inbound
 * request, allowing the host to correlate request/response pairs and to fail
 * fast on corruption.
 *
 * @param session Session context to update.
 */
static inline void hal_serial_session_poll(hal_serial_session_t *session) {
    if ((session == NULL) || (session->module_tag == NULL)) {
        return;
    }

    while (hal_serial_available() > 0) {
        int raw = hal_serial_read();
        if (raw < 0) {
            break;
        }

        char c = (char)raw;
        if ((c != '\r') && (c != '\n')) {
            if (session->line_len < HAL_SERIAL_SESSION_MAX_LINE) {
                session->line[session->line_len++] = c;
            }
            continue;
        }

        if (session->line_len == 0u) {
            continue;
        }

        session->line[session->line_len] = '\0';
        const uint8_t line_len = session->line_len;
        session->line_len = 0u;

        /* Reject anything that is not a framed request — no fall-through. */
        if (line_len < HAL_SERIAL_FRAME_PREFIX_LEN ||
            strncmp(session->line, HAL_SERIAL_FRAME_PREFIX,
                    HAL_SERIAL_FRAME_PREFIX_LEN) != 0) {
            session->line[0] = '\0';
            continue;
        }

        const bool mute_debug = hal_serial_session_should_mute_debug_for_line(session->line);
        const bool debug_was_muted = mute_debug ? hal_debug_is_muted() : false;
        if (mute_debug && !debug_was_muted) {
            hal_debug_set_muted(true);
        }

        uint16_t seq = 0u;
        char inner[HAL_SERIAL_SESSION_MAX_LINE + 1u];
        inner[0] = '\0';
        if (hal_serial_frame_decode(session->line, &seq,
                                    inner, sizeof(inner))) {
            session->in_request = true;
            session->request_seq = seq;
            hal_serial_session__dispatch_inner(session, inner);
            session->in_request = false;
        }
        /* Bad CRC / malformed frame → silent drop (host retries / times out). */

        if (mute_debug && !debug_was_muted) {
            hal_debug_set_muted(false);
        }

        session->line[0] = '\0';
    }
}

#ifdef __cplusplus
}
#endif


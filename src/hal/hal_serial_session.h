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

#include "hal_serial.h"
#include "hal_serial_frame.h"
#include "hal_system.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Current session protocol version. */
#define HAL_SERIAL_SESSION_PROTOCOL_VERSION 1u

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
} hal_serial_session_t;

/**
 * @brief Initialize/clear one serial session context and bind identity fields.
 *
 * The identity fields are captured by pointer and the device UID hex string
 * is captured by value at init time. @p module_tag must remain valid for the
 * lifetime of the session; @p fw_version and @p build_id may be NULL and will
 * be reported as @ref HAL_SERIAL_SESSION_UNKNOWN.
 *
 * @param session     Session context to reset.
 * @param module_tag  Short module identifier (e.g. "ECU"), must not be NULL.
 * @param fw_version  Firmware version string, or NULL.
 * @param build_id    Build identifier string, or NULL.
 */
static inline void hal_serial_session_init(hal_serial_session_t *session,
                                           const char *module_tag,
                                           const char *fw_version,
                                           const char *build_id) {
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
    session->module_tag = (module_tag != NULL) ? module_tag : HAL_SERIAL_SESSION_UNKNOWN;
    session->fw_version = ((fw_version != NULL) && (fw_version[0] != '\0'))
                              ? fw_version : HAL_SERIAL_SESSION_UNKNOWN;
    session->build_id   = ((build_id != NULL) && (build_id[0] != '\0'))
                              ? build_id : HAL_SERIAL_SESSION_UNKNOWN;
    if (!hal_get_device_uid_hex(session->uid_hex, sizeof(session->uid_hex))) {
        session->uid_hex[0] = '\0';
    }
    session->unknown_handler = NULL;
    session->unknown_user = NULL;
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
        hal_serial_session__emit_hello(session);
        return;
    }

    if (session->unknown_handler != NULL) {
        session->unknown_handler(inner, session->unknown_user);
    } else {
        hal_serial_session_println(session, "SC_UNKNOWN_CMD");
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


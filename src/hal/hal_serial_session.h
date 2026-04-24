#pragma once

/**
 * @file hal_serial_session.h
 * @brief Minimal text-based serial session helper for desktop tooling.
 *
 * This helper provides a tiny session state machine intended as a bootstrap
 * transport for future configurator protocols.
 *
 * Current command set:
 * - `HELLO`
 *
 * Current responses:
 * - `OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>`
 * - `ERR UNKNOWN`
 *
 * Design intent:
 * - Keep command parsing in one shared HAL place.
 * - Let each module own only its module-tag and polling call.
 * - Provide a stable skeleton for future expansion (AUTH/GET/SET/etc.).
 *
 * Identity fields (module tag, firmware version, build id) are immutable for
 * the lifetime of a session and are therefore captured at init time rather
 * than passed on every poll. The device UID is sourced from
 * @ref hal_get_device_uid_hex and is also captured at init.
 */

#include "hal_serial.h"
#include "hal_system.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Current plain-text session protocol version. */
#define HAL_SERIAL_SESSION_PROTOCOL_VERSION 1u

/** @brief Maximum accepted command line length (excluding terminator). */
#define HAL_SERIAL_SESSION_MAX_LINE 48u

/** @brief Fallback string for fw/build fields when the caller passes NULL/""  */
#define HAL_SERIAL_SESSION_UNKNOWN "unknown"

/**
 * @brief Optional callback invoked when an incoming line is not recognised
 *        as a built-in session command.
 *
 * When registered via @ref hal_serial_session_set_unknown_handler, the helper
 * stops emitting the default `ERR UNKNOWN` reply and instead forwards the
 * full line (NUL-terminated, without trailing CR/LF) to this callback. This
 * allows higher-level modules (e.g. test fixtures, application command
 * handlers) to consume serial commands only after the bootstrap session
 * parser has had its chance to handle them, preventing two consumers from
 * fighting over individual bytes on the same UART/CDC stream.
 *
 * The callback is responsible for any reply it wishes to send.
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
 * @brief Process all pending serial bytes and handle supported commands.
 *
 * The parser is line-oriented (`\\n` and/or `\\r` terminate command line).
 * Unknown commands currently return `ERR UNKNOWN`.
 *
 * Identity fields reported in the HELLO response come from the session
 * context (populated by @ref hal_serial_session_init).
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
        if ((c == '\r') || (c == '\n')) {
            if (session->line_len == 0u) {
                continue;
            }

            session->line[session->line_len] = '\0';
            if (strcmp(session->line, "HELLO") == 0) {
                session->active = true;
                session->hello_counter++;
                session->last_activity_ms = hal_millis();
                /* Non-cryptographic seed for bootstrap session tracking. */
                session->session_id =
                    ((uint32_t)session->hello_counter << 20) ^
                    (session->last_activity_ms & 0x000FFFFFu);

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
                hal_serial_println(response);
            } else {
                if (session->unknown_handler != NULL) {
                    session->unknown_handler(session->line, session->unknown_user);
                } else {
                    hal_serial_println("ERR UNKNOWN");
                }
            }

            session->line_len = 0u;
            session->line[0] = '\0';
            continue;
        }

        if (session->line_len < HAL_SERIAL_SESSION_MAX_LINE) {
            session->line[session->line_len++] = c;
        }
    }
}

#ifdef __cplusplus
}
#endif


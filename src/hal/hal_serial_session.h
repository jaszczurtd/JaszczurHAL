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
 * - `OK HELLO module=<name> proto=1 session=<id>`
 * - `ERR UNKNOWN`
 *
 * Design intent:
 * - Keep command parsing in one shared HAL place.
 * - Let each module own only its module-tag and polling call.
 * - Provide a stable skeleton for future expansion (AUTH/GET/SET/etc.).
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

/**
 * @brief Runtime state for one serial session endpoint.
 */
typedef struct {
    bool active;                               /**< true after successful HELLO handshake. */
    uint32_t session_id;                       /**< Last assigned session id. */
    uint32_t hello_counter;                    /**< Monotonic counter of accepted HELLO commands. */
    uint32_t last_activity_ms;                 /**< Last time a command was processed. */
    uint8_t line_len;                          /**< Current receive line length. */
    char line[HAL_SERIAL_SESSION_MAX_LINE + 1u]; /**< Command line buffer (NUL-terminated on parse). */
} hal_serial_session_t;

/**
 * @brief Initialize/clear one serial session context.
 * @param session Session context to reset.
 */
static inline void hal_serial_session_init(hal_serial_session_t *session) {
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
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
 * @param session Session context to update.
 * @param module_tag Short module identifier returned in HELLO response
 *                   (for example: `"ECU"`, `"CLOCKS"`, `"OILANDSPEED"`).
 */
static inline void hal_serial_session_poll(hal_serial_session_t *session, const char *module_tag) {
    if ((session == NULL) || (module_tag == NULL)) {
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

                char response[96];
                snprintf(response, sizeof(response),
                         "OK HELLO module=%s proto=%u session=%lu",
                         module_tag,
                         (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
                         (unsigned long)session->session_id);
                hal_serial_println(response);
            } else {
                hal_serial_println("ERR UNKNOWN");
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


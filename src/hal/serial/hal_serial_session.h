#pragma once

/**
 * @file hal_serial_session.h
 * @brief Framed serial session API for SC-style serial transports.
 *
 * The compiled implementation accepts only frames described by
 * @ref hal_serial_frame.h:
 *
 *     $SC,<seq>,<inner>*<crc8>\n
 *
 * `HELLO` is structural. Optional BYE, authentication and bootloader commands
 * are supplied through @ref hal_serial_session_vocabulary_t. Unrecognised
 * inner payloads can be delegated to a project callback.
 */

#include "hal/core/hal_config.h"
#include "hal/serial/hal_serial_session_vocabulary.h"
#include "hal/system/hal_system.h"
#ifdef HAL_ENABLE_CRYPTO
#include "hal/security/hal_sc_auth.h"
#endif

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Current session protocol version. */
#define HAL_SERIAL_SESSION_PROTOCOL_VERSION 1u

/** @brief Maximum accepted command line length, excluding its terminator. */
#define HAL_SERIAL_SESSION_MAX_LINE 128u

/** @brief Fallback value for missing firmware/build identity strings. */
#define HAL_SERIAL_SESSION_UNKNOWN "unknown"

/**
 * @brief Resolve a vocabulary field for a session, including NULL fallback.
 */
#define HAL_SERIAL_SESSION_VOCAB(session, field)                               \
  HAL_SERIAL_SESSION_VOCAB_FIELD((session)->vocab, field)

/**
 * @brief Callback for inner payloads not handled by the session engine.
 *
 * The line is NUL-terminated and already removed from its wire frame. Replies
 * should use @ref hal_serial_session_println so they inherit the request
 * sequence number.
 */
typedef void (*hal_serial_session_unknown_cb_t)(const char *line, void *user);

/**
 * @brief Runtime state for one framed serial endpoint.
 *
 * Identity and vocabulary pointers are borrowed and must remain valid for the
 * session lifetime. Applications should treat the remaining fields as opaque
 * state owned by the compiled protocol engine.
 */
typedef struct {
  bool active;
  uint32_t session_id;
  uint32_t hello_counter;
  uint32_t last_activity_ms;
  uint8_t line_len;
  char line[HAL_SERIAL_SESSION_MAX_LINE + 1u];
  const char *module_tag;
  const char *fw_version;
  const char *build_id;
  char uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE];
  hal_serial_session_unknown_cb_t unknown_handler;
  void *unknown_user;
  bool in_request;
  uint16_t request_seq;
  const hal_serial_session_vocabulary_t *vocab;
#ifdef HAL_ENABLE_CRYPTO
  uint8_t uid_bytes[HAL_DEVICE_UID_BYTES];
  bool authenticated;
  bool challenge_pending;
  uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
  uint32_t auth_counter;
  uint32_t auth_failures;
#endif
} hal_serial_session_t;

/**
 * @brief Reset a session and bind identity plus an optional vocabulary.
 *
 * @param session    Session context to initialize.
 * @param module_tag Module identifier; NULL becomes @ref
 *                   HAL_SERIAL_SESSION_UNKNOWN.
 * @param fw_version Firmware version; NULL/empty becomes `unknown`.
 * @param build_id   Build identifier; NULL/empty becomes `unknown`.
 * @param vocab      Optional vocabulary override; NULL enables HELLO only.
 */
void hal_serial_session_init_with_vocabulary(
    hal_serial_session_t *session, const char *module_tag,
    const char *fw_version, const char *build_id,
    const hal_serial_session_vocabulary_t *vocab);

/** @brief Initialize a HELLO-only session with the empty default vocabulary. */
void hal_serial_session_init(hal_serial_session_t *session,
                             const char *module_tag, const char *fw_version,
                             const char *build_id);

/** @brief Register or clear the callback for unrecognised inner payloads. */
void hal_serial_session_set_unknown_handler(hal_serial_session_t *session,
                                            hal_serial_session_unknown_cb_t cb,
                                            void *user);

/** @brief Return whether HELLO has activated the session. */
bool hal_serial_session_is_active(const hal_serial_session_t *session);

/** @brief Return the current session id, or zero for a NULL session. */
uint32_t hal_serial_session_id(const hal_serial_session_t *session);

/**
 * @brief Return whether the current session completed authentication.
 *
 * Always returns false when @c HAL_ENABLE_CRYPTO is disabled.
 */
bool hal_serial_session_is_authenticated(const hal_serial_session_t *session);

/** @brief Return whether a line is a framed request that should mute debug. */
bool hal_serial_session_should_mute_debug_for_line(const char *line);

/**
 * @brief Emit one framed reply during the current request dispatch window.
 *
 * Calls made outside @ref hal_serial_session_poll dispatch are ignored.
 */
void hal_serial_session_println(hal_serial_session_t *session,
                                const char *payload);

/** @brief Consume and dispatch every currently available serial frame. */
void hal_serial_session_poll(hal_serial_session_t *session);

#ifdef __cplusplus
}
#endif

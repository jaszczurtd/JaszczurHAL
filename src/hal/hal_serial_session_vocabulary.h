#pragma once

/**
 * @file hal_serial_session_vocabulary.h
 * @brief Vocabulary table for @ref hal_serial_session.
 *
 * The framed serial session helper recognises a small set of inbound command
 * tokens and emits a matching set of outbound reply tokens. Their actual
 * spellings are project-specific (Fiesta, for example, uses the SC_* dialect)
 * and live OUTSIDE JaszczurHAL: the host project provides them at session init
 * time via @ref hal_serial_session_init_with_vocabulary.
 *
 * R1.6 (2026-04-27) finished the decoupling: this header no longer carries any
 * SC_* literals. @ref hal_serial_session_vocabulary_default is a placeholder
 * with every field NULL, which the dispatch path reads as "this command is not
 * recognised / this reply is not emitted". Projects that need the AUTH /
 * REBOOT_BOOTLOADER handlers MUST pass a populated vocabulary instance; the
 * Fiesta default lives in `src/common/scDefinitions/sc_session_vocabulary.h`
 * (`fiesta_default_vocabulary`).
 *
 * The classic @ref hal_serial_session_init entry point still works for
 * HELLO-only sessions: HELLO is structural and intentionally NOT configurable
 * (see below). It just won't recognise AUTH / REBOOT commands, which fall
 * through to the unknown-line handler.
 *
 * Design notes:
 * - HELLO is structural and intentionally NOT configurable here. Hosts parse
 *   the `OK HELLO module=... proto=... session=... fw=... build=... uid=...`
 *   line by structure; renaming the keyword would break every host.
 * - Reply strings ending in @c _fmt are passed to @c printf-family formatters.
 *   The %s/%u placeholders MUST be preserved by overrides; format mismatch is
 *   undefined behaviour.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Vocabulary table for @ref hal_serial_session.
 *
 * Each field is independently optional: NULL falls back to the corresponding
 * field of @ref hal_serial_session_vocabulary_default. Pointer storage is
 * borrowed - strings must remain valid for the lifetime of the session.
 */
typedef struct {
    /* Inbound command tokens (matched verbatim, except cmd_auth_prove which
     * is a prefix followed by a space and the response hex). */
    const char *cmd_bye;
    const char *cmd_auth_begin;
    const char *cmd_auth_prove;
    const char *cmd_reboot_bootloader;

    /* Outbound reply payloads. */
    const char *reply_bye_ok;
    const char *reply_unknown_cmd;
    const char *reply_not_ready_hello_required;
    /** Format string carrying one %s for the hex challenge. */
    const char *reply_auth_challenge_fmt;
    const char *reply_auth_ok;
    const char *reply_auth_failed_no_challenge;
    const char *reply_auth_failed_bad_length;
    const char *reply_auth_failed_bad_hex;
    const char *reply_auth_failed_key_derivation;
    const char *reply_auth_failed_mac_compute;
    const char *reply_auth_failed_bad_mac;
    const char *reply_not_authorized;
    const char *reply_reboot_ok;
} hal_serial_session_vocabulary_t;

/**
 * @brief Empty placeholder vocabulary - every field is NULL.
 *
 * Defined as @c static @c const in the header so consumers can resolve
 * NULL-fallbacks at compile time without a dedicated translation unit.
 *
 * R1.6 stripped the historical SC_* defaults. The dispatch path reads a
 * NULL command field as "this command is not recognised by this session"
 * (the inner payload falls through to the unknown-line handler) and a
 * NULL reply field as "this reply is not emitted by this session"
 * (silent drop). Callers that want AUTH / REBOOT handling MUST pass a
 * fully populated vocabulary via
 * @ref hal_serial_session_init_with_vocabulary.
 */
static const hal_serial_session_vocabulary_t
    hal_serial_session_vocabulary_default = {
        .cmd_bye = NULL,
        .cmd_auth_begin = NULL,
        .cmd_auth_prove = NULL,
        .cmd_reboot_bootloader = NULL,
        .reply_bye_ok = NULL,
        .reply_unknown_cmd = NULL,
        .reply_not_ready_hello_required = NULL,
        .reply_auth_challenge_fmt = NULL,
        .reply_auth_ok = NULL,
        .reply_auth_failed_no_challenge = NULL,
        .reply_auth_failed_bad_length = NULL,
        .reply_auth_failed_bad_hex = NULL,
        .reply_auth_failed_key_derivation = NULL,
        .reply_auth_failed_mac_compute = NULL,
        .reply_auth_failed_bad_mac = NULL,
        .reply_not_authorized = NULL,
        .reply_reboot_ok = NULL,
};

/**
 * @brief Resolve one vocabulary field with per-field NULL fallback.
 *
 * Returns the override from @p vocab_ptr when both @p vocab_ptr and its
 * @p field are non-NULL; otherwise returns the corresponding field of
 * @ref hal_serial_session_vocabulary_default.
 *
 * @note The macro evaluates @p vocab_ptr twice; pass a side-effect-free
 *       lvalue (typically @c session->vocab).
 */
#define HAL_SERIAL_SESSION_VOCAB_FIELD(vocab_ptr, field)                        \
    (((vocab_ptr) != NULL && (vocab_ptr)->field != NULL)                        \
         ? (vocab_ptr)->field                                                   \
         : hal_serial_session_vocabulary_default.field)

#ifdef __cplusplus
}
#endif

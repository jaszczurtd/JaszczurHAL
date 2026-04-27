#pragma once

/**
 * @file hal_serial_session_vocabulary.h
 * @brief Optional vocabulary table for @ref hal_serial_session.
 *
 * The framed serial session helper recognises a small set of inbound command
 * tokens (SC_AUTH_BEGIN, SC_AUTH_PROVE, SC_REBOOT_BOOTLOADER) and emits a
 * matching set of outbound reply tokens (SC_OK AUTH_OK, SC_AUTH_FAILED ...,
 * SC_NOT_AUTHORIZED, ...). Historically those literals were hard-coded inside
 * the helper, baking Fiesta-specific protocol vocabulary into JaszczurHAL.
 *
 * A @ref hal_serial_session_vocabulary_t lets callers pass the whole vocabulary
 * via @ref hal_serial_session_init_with_vocabulary, decoupling the helper from
 * any one project. NULL fields fall back per-field to the historical defaults
 * via @ref hal_serial_session_vocabulary_default, so adopters can override only
 * the tokens they care about. The classic @ref hal_serial_session_init entry
 * point keeps working unchanged: it stores a NULL vocab pointer and every
 * lookup falls back to the default.
 *
 * Design notes:
 * - HELLO is structural and intentionally NOT configurable here. Hosts parse
 *   the `OK HELLO module=... proto=... session=... fw=... build=... uid=...`
 *   line by structure; renaming the keyword would break every host.
 * - Reply strings ending in @c _fmt are passed to @c printf-family formatters.
 *   The %s/%u placeholders in the default match the historical wire format and
 *   MUST be preserved by overrides; format mismatch is undefined behaviour.
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
 * borrowed — strings must remain valid for the lifetime of the session.
 */
typedef struct {
    /* Inbound command tokens (matched verbatim, except cmd_auth_prove which
     * is a prefix followed by a space and the response hex). */
    const char *cmd_auth_begin;
    const char *cmd_auth_prove;
    const char *cmd_reboot_bootloader;

    /* Outbound reply payloads. */
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
 * @brief Default vocabulary — the SC tokens hard-coded into
 *        @ref hal_serial_session prior to R1.0.
 *
 * Defined as @c static @c const in the header so consumers can resolve
 * NULL-fallbacks at compile time without a dedicated translation unit.
 * String literal merging dedupes the underlying chars across TUs; the
 * 15-pointer struct itself is small enough that the per-TU copy is not
 * worth optimising.
 */
static const hal_serial_session_vocabulary_t
    hal_serial_session_vocabulary_default = {
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

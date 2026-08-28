#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_SERIAL_COMMANDS

/**
 * @file hal_serial_commands.h
 * @brief Text command-router adapter for framed serial sessions.
 *
 * The adapter occupies one session's unknown-payload callback. Structural
 * HELLO, BYE and authentication messages remain owned by
 * @ref hal_serial_session. Other matching payloads are split into a command
 * name and optional arguments, dispatched synchronously, then returned in an
 * SC frame with the request sequence number. Separating whitespace is removed
 * when followed by arguments; a whitespace-only suffix is preserved so a
 * handler can enforce exact no-argument commands.
 */

#include "hal/commands/hal_command_router.h"
#include "hal/serial/hal_serial_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Format a response that has no directly usable text body.
 *
 * The callback receives router request/response views valid only for the
 * duration of the call. It writes at most @p output_capacity bytes, reports
 * their count through @p out_length, and need not append a NUL terminator.
 * Output must be valid SC payload text without `*`, CR, LF or embedded NUL.
 */
typedef hal_status_t (*hal_serial_commands_response_formatter_t)(
    const hal_command_request_t *request,
    const hal_command_response_t *response, char *output,
    size_t output_capacity, size_t *out_length, void *user);

/**
 * @brief Decide whether one matching request may reach the router before HELLO.
 *
 * Returning true does not bypass router source or security policy. The default
 * without this callback is false for every command.
 */
typedef bool (*hal_serial_commands_allow_inactive_t)(
    const hal_command_request_t *request, void *user);

/** @brief Construction settings copied by @ref hal_serial_commands_init. */
typedef struct {
  hal_serial_session_t *session;
  /** NULL selects the process-wide default router. */
  hal_command_router_t router;
  /** Application-defined identity for the physical serial endpoint. */
  uint64_t peer_id;
  /** Serial commands support TEXT and JSON arguments only. */
  hal_command_encoding_t encoding;
  /**
   * Optional borrowed literal prefix used only to select router commands.
   * The prefix remains part of the command name. NULL or empty selects every
   * unknown session payload.
   */
  const char *command_prefix;
  /** Optional formatter for empty or non-text router responses. */
  hal_serial_commands_response_formatter_t formatter;
  void *formatter_user;
  /** Optional pre-HELLO admission predicate for selected router commands. */
  hal_serial_commands_allow_inactive_t allow_inactive;
  void *allow_inactive_user;
  /** Optional sink for payloads that do not match command_prefix. */
  hal_serial_session_unknown_cb_t fallback;
  void *fallback_user;
} hal_serial_commands_config_t;

/**
 * @brief Caller-owned state for one serial command adapter.
 *
 * Zero-initialize before first use. The session, router, prefix and callback
 * storage are borrowed and must outlive the initialized adapter.
 */
typedef struct {
  hal_serial_commands_config_t config;
  hal_status_t last_status;
  bool dispatch_active;
  bool initialized;
} hal_serial_commands_t;

/** @brief Return defaults for one session: TEXT, default router, no prefix. */
hal_serial_commands_config_t
hal_serial_commands_config_defaults(hal_serial_session_t *session);

/**
 * @brief Attach one adapter to an initialized serial session.
 *
 * Returns @ref HAL_EBUSY when the session already has an unknown handler.
 */
hal_status_t
hal_serial_commands_init(hal_serial_commands_t *commands,
                         const hal_serial_commands_config_t *config);

/**
 * @brief Detach an idle adapter without replacing a callback installed later.
 *
 * Returns @ref HAL_EBUSY while any adapter callback or dispatch is active, or
 * when another callback owns the session slot. Returns @ref HAL_ENOENT if the
 * session callback was removed.
 */
hal_status_t hal_serial_commands_deinit(hal_serial_commands_t *commands);

/** @brief Copy the result of the most recent matched or fallback payload. */
hal_status_t
hal_serial_commands_get_last_status(const hal_serial_commands_t *commands,
                                    hal_status_t *out_status);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_SERIAL_COMMANDS */

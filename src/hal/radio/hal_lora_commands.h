#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LORA_COMMANDS

/**
 * @file hal_lora_commands.h
 * @brief Command-router transport over one reliable LoRa link.
 *
 * One adapter exclusively advances and receives from its link. The link and
 * router remain owned by the caller and must outlive the adapter.
 */

#include "hal/commands/hal_command_wire.h"
#include "hal/radio/hal_lora_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque LoRa command-adapter handle. */
typedef struct hal_lora_commands_impl_s *hal_lora_commands_t;

/** @brief Construction descriptor for one command adapter. */
typedef struct {
  hal_lora_link_t link;
  /** NULL selects the process-wide default command router. */
  hal_command_router_t router;
  uint8_t port;
  bool acknowledged;
  /** First locally generated request identifier; zero is invalid. */
  uint32_t initial_request_id;
} hal_lora_commands_config_t;

/** @brief Current adapter and underlying-link state. */
typedef struct {
  hal_lora_link_state_t link_state;
  hal_lora_link_send_status_t link_send;
  hal_lora_link_message_info_t last_received;
  uint32_t next_request_id;
  uint16_t pending_response_destination;
  uint8_t port;
  bool acknowledged;
  bool pending_response;
  bool receive_ready;
  bool process_active;
  bool dispatch_active;
} hal_lora_commands_info_t;

/** @brief Per-adapter command traffic and error counters. */
typedef struct {
  uint32_t process_calls;
  uint32_t requests_sent;
  uint32_t requests_received;
  uint32_t responses_sent;
  uint32_t responses_received;
  uint32_t events_sent;
  uint32_t events_received;
  uint32_t dispatch_failures;
  uint32_t protocol_errors;
  uint32_t wrong_port_messages;
  uint32_t dropped_messages;
  uint32_t pending_response_retries;
  uint32_t response_send_failures;
  hal_status_t last_dispatch_status;
  hal_status_t last_error;
} hal_lora_commands_diagnostics_t;

/** @brief Return defaults for one link and application-defined port. */
hal_lora_commands_config_t
hal_lora_commands_config_defaults(hal_lora_link_t link, uint8_t port);

/**
 * @brief Attach a command adapter to an initialized link.
 *
 * Only one adapter may own a link. Once attached, callers must use
 * hal_lora_commands_process() instead of calling hal_lora_link_process() or
 * hal_lora_link_receive() directly.
 */
hal_status_t hal_lora_commands_create(const hal_lora_commands_config_t *config,
                                      hal_lora_commands_t *out_commands);

/**
 * @brief Release an adapter without destroying its link or router.
 *
 * HAL_EBUSY is returned while processing or dispatch is active, a response is
 * pending, an application message is unread, or the link is not receiving.
 * A successful call invalidates the handle; later calls return HAL_EUNINIT.
 */
hal_status_t hal_lora_commands_destroy(hal_lora_commands_t commands);

/**
 * @brief Start one copied command request.
 *
 * The generated non-zero identifier is written only when the link accepts the
 * send. HAL_EBUSY and HAL_EAGAIN leave the identifier available for retry.
 */
hal_status_t hal_lora_commands_request_start(
    hal_lora_commands_t commands, uint16_t destination, const char *command,
    hal_command_encoding_t encoding, const void *arguments,
    size_t arguments_length, uint32_t *out_request_id);

/**
 * @brief Start one copied event message with request identifier zero.
 *
 * Broadcast events are always sent without a transport acknowledgement.
 */
hal_status_t hal_lora_commands_event_start(hal_lora_commands_t commands,
                                           uint16_t destination,
                                           const char *event,
                                           hal_command_encoding_t encoding,
                                           const void *payload,
                                           size_t payload_length);

/**
 * @brief Advance link I/O and command handling.
 *
 * Incoming requests are dispatched synchronously. Their responses remain in
 * an adapter-owned buffer while the link is busy sending the transport ACK.
 * A handler may call the information, diagnostics, receive, request-start and
 * event-start functions. Reentrant process and destroy calls return HAL_EBUSY.
 */
hal_status_t hal_lora_commands_process(hal_lora_commands_t commands);

/**
 * @brief Copy and consume one received RESPONSE or EVENT.
 *
 * The optional link metadata identifies the sender and radio observations.
 * HAL_EAGAIN means that no application-visible message is queued.
 */
hal_status_t
hal_lora_commands_receive(hal_lora_commands_t commands,
                          hal_command_message_t *out_message,
                          hal_lora_link_message_info_t *out_link_info);

/** @brief Copy a current adapter and link snapshot. */
hal_status_t hal_lora_commands_get_info(hal_lora_commands_t commands,
                                        hal_lora_commands_info_t *out_info);

/** @brief Copy per-adapter counters. */
hal_status_t hal_lora_commands_get_diagnostics(
    hal_lora_commands_t commands,
    hal_lora_commands_diagnostics_t *out_diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_LORA_COMMANDS */

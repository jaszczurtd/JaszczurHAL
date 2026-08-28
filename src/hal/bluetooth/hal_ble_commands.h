#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLE_COMMANDS

/**
 * @file hal_ble_commands.h
 * @brief Command-router adapter over one authenticated JH BLE Stream session.
 *
 * The adapter owns command-wire consumption from the process-wide BLE Stream.
 * The caller retains ownership of BLE, the Stream service and the router, and
 * remains responsible for calling hal_ble_poll().
 */

#include "hal/bluetooth/hal_ble_stream.h"
#include "hal/commands/hal_command_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS
#define HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS 5000u
#endif

#if HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS == 0u
#error "HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS must be greater than zero"
#endif

/** @brief Opaque BLE command-adapter handle. */
typedef struct hal_ble_commands_impl_s *hal_ble_commands_t;

/** @brief Construction descriptor for the single BLE Stream adapter. */
typedef struct {
  /** NULL selects the process-wide default command router. */
  hal_command_router_t router;
  /** First locally generated request identifier; zero is invalid. */
  uint32_t initial_request_id;
  /** Incomplete command-wire lifetime; zero selects the default. */
  uint32_t partial_frame_timeout_ms;
} hal_ble_commands_config_t;

/**
 * @brief Authenticated peer snapshot used for dispatch and received messages.
 *
 * During handler execution, hal_command_request_t::source_context points to a
 * value of this type. The pointer is borrowed and valid only for that call.
 */
typedef struct {
  hal_ble_address_t peer_address;
  hal_ble_connection_handle_t connection;
  uint16_t mtu;
  uint16_t negotiated_capabilities;
  uint32_t ble_generation;
  uint32_t stream_generation;
  uint64_t session_id;
  uint64_t first_rx_counter;
  uint64_t last_rx_counter;
  hal_command_security_flags_t security_flags;
} hal_ble_commands_peer_info_t;

/** @brief Current adapter and BLE Stream state. */
typedef struct {
  hal_ble_stream_state_t stream_state;
  uint64_t session_id;
  uint32_t next_request_id;
  size_t receive_buffered;
  size_t transmit_length;
  size_t transmit_offset;
  bool pending_response;
  bool receive_ready;
  bool process_active;
  bool dispatch_active;
} hal_ble_commands_info_t;

/** @brief Per-adapter command traffic and error counters. */
typedef struct {
  uint32_t process_calls;
  uint32_t session_starts;
  uint32_t session_resets;
  uint32_t requests_started;
  uint32_t requests_sent;
  uint32_t requests_received;
  uint32_t responses_sent;
  uint32_t responses_received;
  uint32_t events_started;
  uint32_t events_sent;
  uint32_t events_received;
  uint32_t stream_chunks_sent;
  uint32_t stream_chunks_received;
  uint32_t send_retries;
  uint32_t partial_frame_timeouts;
  uint32_t dispatch_failures;
  uint32_t protocol_errors;
  uint32_t dropped_messages;
  hal_status_t last_dispatch_status;
  hal_status_t last_error;
} hal_ble_commands_diagnostics_t;

/** @brief Return defaults for the process-wide BLE Stream. */
hal_ble_commands_config_t hal_ble_commands_config_defaults(void);

/**
 * @brief Attach the command adapter to an initialized BLE Stream service.
 *
 * Only one adapter may consume the process-wide Stream. The Stream may be idle
 * at creation time; command traffic becomes available after authentication.
 */
hal_status_t hal_ble_commands_create(const hal_ble_commands_config_t *config,
                                     hal_ble_commands_t *out_commands);

/**
 * @brief Release an idle adapter without deinitializing BLE or BLE Stream.
 *
 * Pending wire data and unread application messages must first be processed or
 * consumed. A successful call invalidates the handle.
 */
hal_status_t hal_ble_commands_destroy(hal_ble_commands_t commands);

/**
 * @brief Queue one copied request for the authenticated peer.
 *
 * The identifier advances when the bounded wire message is accepted by the
 * adapter. Delivery is completed incrementally by hal_ble_commands_process().
 */
hal_status_t hal_ble_commands_request_start(hal_ble_commands_t commands,
                                            const char *command,
                                            hal_command_encoding_t encoding,
                                            const void *arguments,
                                            size_t arguments_length,
                                            uint32_t *out_request_id);

/** @brief Queue one copied event for the authenticated peer. */
hal_status_t hal_ble_commands_event_start(hal_ble_commands_t commands,
                                          const char *event,
                                          hal_command_encoding_t encoding,
                                          const void *payload,
                                          size_t payload_length);

/**
 * @brief Advance framing, dispatch and automatic response transmission.
 *
 * Call hal_ble_poll() separately to service controller and Stream I/O. Each
 * call performs bounded work: at most one outgoing and one incoming Stream
 * chunk, plus at most one synchronous command dispatch.
 */
hal_status_t hal_ble_commands_process(hal_ble_commands_t commands);

/**
 * @brief Copy and consume one received RESPONSE or EVENT.
 *
 * HAL_EAGAIN means no application-visible message is queued.
 */
hal_status_t
hal_ble_commands_receive(hal_ble_commands_t commands,
                         hal_command_message_t *out_message,
                         hal_ble_commands_peer_info_t *out_peer_info);

/** @brief Copy a current adapter and Stream snapshot. */
hal_status_t hal_ble_commands_get_info(hal_ble_commands_t commands,
                                       hal_ble_commands_info_t *out_info);

/** @brief Copy per-adapter counters. */
hal_status_t hal_ble_commands_get_diagnostics(
    hal_ble_commands_t commands,
    hal_ble_commands_diagnostics_t *out_diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLE_COMMANDS */

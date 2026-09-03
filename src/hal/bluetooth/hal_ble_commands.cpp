#include "hal/bluetooth/hal_ble_commands.h"

#ifdef HAL_ENABLE_BLE_COMMANDS

#include "hal/bluetooth/jh_ble_stream_runtime.h"
#include "hal/commands/jh_command_adapter_internal.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/security/jh_secure_random.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <string.h>

#define JH_BLE_COMMANDS_HANDLE_KIND 13u
#define JH_BLE_COMMANDS_INSTANCE_COUNT 1u

namespace {

struct jh_ble_commands_context_t {
  hal_ble_commands_config_t config;
  hal_mutex_t mutex;
  hal_ble_commands_diagnostics_t diagnostics;
  hal_ble_commands_peer_info_t peer;
  hal_ble_commands_peer_info_t receive_peer;
  hal_ble_commands_peer_info_t received_peer;
  hal_ble_commands_peer_info_t dispatch_peer;
  hal_command_message_t scratch_message;
  hal_command_message_t dispatch_message;
  hal_command_message_t received_message;
  hal_command_response_t handler_response;
  uint8_t receive_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE +
                       HAL_BLE_STREAM_MAX_PAYLOAD - 1u];
  uint8_t transmit_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE];
  uint8_t pending_response_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE];
  size_t receive_length;
  size_t transmit_length;
  size_t transmit_offset;
  size_t pending_response_length;
  uint8_t transmit_type;
  uint32_t next_request_id;
  uint32_t partial_frame_started_ms;
  bool session_active;
  bool received_ready;
  bool process_active;
  bool dispatch_active;
  bool allocated;
};

jh_ble_commands_context_t s_context{};
jh_handle_slot_t s_handle_slot{};
jh_handle_pool_t s_handle_pool{};
hal_mutex_t s_pool_mutex = nullptr;
bool s_pool_initialized = false;

hal_status_t pool_lock() {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    const hal_status_t status = jh_handle_pool_init(
        &s_handle_pool, &s_handle_slot, JH_BLE_COMMANDS_INSTANCE_COUNT,
        JH_BLE_COMMANDS_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

void pool_unlock() { hal_mutex_unlock(s_pool_mutex); }

void clear_context(void *token) {
  auto *context = static_cast<jh_ble_commands_context_t *>(token);
  hal_mutex_t mutex = context->mutex;
  jh_secure_zeroize(context, sizeof(*context));
  context->mutex = mutex;
  context->diagnostics.last_dispatch_status = HAL_NONE;
  context->diagnostics.last_error = HAL_NONE;
}

const jh_command_adapter_context_access_t s_context_access{
    &s_handle_pool,
    pool_lock,
    pool_unlock,
    jh_command_adapter_context_mutex<jh_ble_commands_context_t>,
    jh_command_adapter_context_allocated<jh_ble_commands_context_t>,
    clear_context};

hal_status_t context_lock(hal_ble_commands_t commands,
                          jh_command_adapter_operation_t *operation) {
  return jh_command_adapter_context_lock(&s_context_access, commands,
                                         operation);
}

hal_status_t finish_operation(jh_command_adapter_operation_t *operation,
                              hal_status_t status) {
  return jh_command_adapter_operation_finish(operation, status);
}

void record_error(jh_ble_commands_context_t *context, hal_status_t status) {
  if (jh_command_adapter_status_is_hard(status)) {
    context->diagnostics.last_error = status;
  }
}

void clear_transfer_state(jh_ble_commands_context_t *context) {
  jh_secure_zeroize(context->receive_wire, sizeof(context->receive_wire));
  jh_secure_zeroize(context->transmit_wire, sizeof(context->transmit_wire));
  jh_secure_zeroize(context->pending_response_wire,
                    sizeof(context->pending_response_wire));
  jh_secure_zeroize(&context->receive_peer, sizeof(context->receive_peer));
  jh_secure_zeroize(&context->received_peer, sizeof(context->received_peer));
  jh_secure_zeroize(&context->scratch_message,
                    sizeof(context->scratch_message));
  jh_secure_zeroize(&context->received_message,
                    sizeof(context->received_message));
  if (!context->dispatch_active) {
    jh_secure_zeroize(&context->dispatch_peer, sizeof(context->dispatch_peer));
    jh_secure_zeroize(&context->dispatch_message,
                      sizeof(context->dispatch_message));
    jh_secure_zeroize(&context->handler_response,
                      sizeof(context->handler_response));
  }
  context->receive_length = 0u;
  context->transmit_length = 0u;
  context->transmit_offset = 0u;
  context->pending_response_length = 0u;
  context->transmit_type = 0u;
  context->partial_frame_started_ms = 0u;
  context->received_ready = false;
}

void leave_session(jh_ble_commands_context_t *context) {
  if (context->session_active) {
    ++context->diagnostics.session_resets;
  }
  context->session_active = false;
  jh_secure_zeroize(&context->peer, sizeof(context->peer));
  clear_transfer_state(context);
}

uint64_t peer_id(const hal_ble_address_t &address) {
  uint64_t value = (uint64_t)address.type << 56u;
  for (size_t index = 0u; index < HAL_BLE_ADDRESS_LEN; ++index) {
    value |= (uint64_t)address.bytes[index]
             << ((HAL_BLE_ADDRESS_LEN - 1u - index) * 8u);
  }
  return value;
}

hal_ble_commands_peer_info_t
make_peer_info(const hal_ble_info_t &ble, const hal_ble_stream_info_t &stream) {
  hal_ble_commands_peer_info_t info{};
  info.peer_address = ble.peer_address;
  info.connection = ble.connection;
  info.mtu = ble.mtu;
  info.negotiated_capabilities = stream.negotiated_capabilities;
  info.ble_generation = ble.generation;
  info.stream_generation = stream.generation;
  info.session_id = stream.session_id;
  info.security_flags = HAL_COMMAND_SECURITY_ALL;
  return info;
}

bool same_session(const hal_ble_commands_peer_info_t &left,
                  const hal_ble_commands_peer_info_t &right) {
  return left.connection == right.connection &&
         left.ble_generation == right.ble_generation &&
         left.stream_generation == right.stream_generation &&
         left.session_id == right.session_id;
}

hal_status_t synchronize_session(jh_ble_commands_context_t *context) {
  hal_ble_stream_info_t stream{};
  hal_status_t status = hal_ble_stream_get_info(&stream);
  if (status != HAL_OK) {
    leave_session(context);
    return status;
  }
  hal_ble_info_t ble{};
  status = hal_ble_get_info(&ble);
  if (status != HAL_OK) {
    leave_session(context);
    return status;
  }
  if (stream.state != HAL_BLE_STREAM_STATE_AUTHENTICATED ||
      ble.state != HAL_BLE_STATE_CONNECTED ||
      ble.connection == HAL_BLE_INVALID_HANDLE) {
    leave_session(context);
    return HAL_EAGAIN;
  }

  hal_ble_commands_peer_info_t current = make_peer_info(ble, stream);
  if (!context->session_active || !same_session(context->peer, current)) {
    if (context->session_active) {
      leave_session(context);
    } else {
      clear_transfer_state(context);
    }
    context->session_active = true;
    context->peer = current;
    ++context->diagnostics.session_starts;
  } else {
    context->peer = current;
  }
  return HAL_OK;
}

hal_status_t
resynchronize_after_session_mismatch(jh_ble_commands_context_t *context) {
  const hal_status_t status = synchronize_session(context);
  if (status != HAL_OK && status != HAL_EAGAIN) {
    record_error(context, status);
    return status;
  }
  record_error(context, HAL_EAUTH);
  return HAL_EAUTH;
}

hal_status_t close_corrupt_session(jh_ble_commands_context_t *context,
                                   hal_status_t status) {
  ++context->diagnostics.protocol_errors;
  ++context->diagnostics.dropped_messages;
  record_error(context, status);
  const hal_status_t close_status =
      hal_ble_stream_close_session(HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
  leave_session(context);
  return close_status == HAL_OK ? status : close_status;
}

size_t maximum_stream_chunk(const jh_ble_commands_context_t *context) {
  constexpr size_t overhead =
      HAL_BLE_STREAM_ATT_OVERHEAD + HAL_BLE_STREAM_FRAME_HEADER_LEN +
      HAL_BLE_STREAM_AEAD_COUNTER_LEN + HAL_BLE_STREAM_AEAD_TAG_LEN;
  if ((size_t)context->peer.mtu <= overhead) {
    return 0u;
  }
  const size_t mtu_payload = (size_t)context->peer.mtu - overhead;
  return mtu_payload < HAL_BLE_STREAM_MAX_PAYLOAD ? mtu_payload
                                                  : HAL_BLE_STREAM_MAX_PAYLOAD;
}

void promote_pending_response(jh_ble_commands_context_t *context) {
  if (context->transmit_length != 0u ||
      context->pending_response_length == 0u) {
    return;
  }
  memcpy(context->transmit_wire, context->pending_response_wire,
         context->pending_response_length);
  context->transmit_length = context->pending_response_length;
  context->transmit_offset = 0u;
  context->transmit_type = HAL_COMMAND_MESSAGE_RESPONSE;
  jh_secure_zeroize(context->pending_response_wire,
                    context->pending_response_length);
  context->pending_response_length = 0u;
}

void complete_transmit(jh_ble_commands_context_t *context) {
  switch (context->transmit_type) {
  case HAL_COMMAND_MESSAGE_REQUEST:
    ++context->diagnostics.requests_sent;
    break;
  case HAL_COMMAND_MESSAGE_RESPONSE:
    ++context->diagnostics.responses_sent;
    break;
  case HAL_COMMAND_MESSAGE_EVENT:
    ++context->diagnostics.events_sent;
    break;
  default:
    break;
  }
  jh_secure_zeroize(context->transmit_wire, context->transmit_length);
  context->transmit_length = 0u;
  context->transmit_offset = 0u;
  context->transmit_type = 0u;
}

hal_status_t send_one_chunk(jh_ble_commands_context_t *context) {
  promote_pending_response(context);
  if (context->transmit_length == 0u) {
    return HAL_EAGAIN;
  }
  const size_t chunk_capacity = maximum_stream_chunk(context);
  if (chunk_capacity == 0u) {
    return HAL_EAGAIN;
  }
  const size_t remaining = context->transmit_length - context->transmit_offset;
  const size_t chunk = remaining < chunk_capacity ? remaining : chunk_capacity;
  const hal_status_t status = jh_ble_stream_send_for_session(
      &context->transmit_wire[context->transmit_offset], chunk,
      context->peer.stream_generation, context->peer.session_id);
  if (status == HAL_OK) {
    context->transmit_offset += chunk;
    ++context->diagnostics.stream_chunks_sent;
    if (context->transmit_offset == context->transmit_length) {
      complete_transmit(context);
    }
    return HAL_OK;
  }
  if (status == HAL_EAGAIN) {
    ++context->diagnostics.send_retries;
    return status;
  }
  if (status == HAL_EAUTH) {
    return resynchronize_after_session_mismatch(context);
  }

  const bool partial = context->transmit_offset != 0u;
  if (partial) {
    return close_corrupt_session(context, status);
  }
  ++context->diagnostics.dropped_messages;
  record_error(context, status);
  jh_secure_zeroize(context->transmit_wire, context->transmit_length);
  context->transmit_length = 0u;
  context->transmit_offset = 0u;
  context->transmit_type = 0u;
  return status;
}

hal_status_t queue_response(jh_ble_commands_context_t *context,
                            uint32_t request_id) {
  if (context->pending_response_length != 0u) {
    return HAL_EBUSY;
  }
  uint8_t *wire = context->transmit_length == 0u
                      ? context->transmit_wire
                      : context->pending_response_wire;
  size_t wire_length = 0u;
  const hal_status_t status = jh_command_adapter_encode_response(
      &context->handler_response, request_id, &context->scratch_message, wire,
      HAL_COMMAND_WIRE_MAX_FRAME_SIZE, &wire_length);
  jh_secure_zeroize(&context->scratch_message,
                    sizeof(context->scratch_message));
  if (status != HAL_OK) {
    jh_secure_zeroize(wire, HAL_COMMAND_WIRE_MAX_FRAME_SIZE);
    ++context->diagnostics.dropped_messages;
    record_error(context, status);
    return status;
  }
  if (context->transmit_length == 0u) {
    context->transmit_length = wire_length;
    context->transmit_offset = 0u;
    context->transmit_type = HAL_COMMAND_MESSAGE_RESPONSE;
  } else {
    context->pending_response_length = wire_length;
  }
  return HAL_OK;
}

hal_status_t dispatch_request(jh_ble_commands_context_t *context) {
  hal_command_request_t request{};
  request.source = HAL_COMMAND_SOURCE_BLE_STREAM;
  request.encoding = context->dispatch_message.encoding;
  request.command = context->dispatch_message.name;
  request.arguments = context->dispatch_message.payload_length == 0u
                          ? nullptr
                          : context->dispatch_message.payload;
  request.arguments_length = context->dispatch_message.payload_length;
  request.request_id = context->dispatch_message.request_id;
  request.peer_id = peer_id(context->dispatch_peer.peer_address);
  request.session_id = context->dispatch_peer.session_id;
  request.security_flags = context->dispatch_peer.security_flags;
  request.source_context = &context->dispatch_peer;

  const uint32_t request_id = request.request_id;
  hal_command_router_t router = context->config.router;
  const hal_ble_commands_peer_info_t expected_peer = context->dispatch_peer;
  context->dispatch_active = true;
  hal_mutex_unlock(context->mutex);
  const hal_status_t dispatch_status =
      hal_command_router_dispatch(router, &request, &context->handler_response);
  hal_mutex_lock(context->mutex);
  context->dispatch_active = false;
  context->diagnostics.last_dispatch_status = dispatch_status;
  if (dispatch_status != HAL_OK) {
    ++context->diagnostics.dispatch_failures;
  }

  hal_status_t status = synchronize_session(context);
  if (status == HAL_OK && !same_session(expected_peer, context->peer)) {
    /* The response belongs to the session that entered the handler. A
       concurrent caller may already have accepted work for a newer session;
       reject only the stale response and preserve that newer transfer. */
    status = HAL_EAUTH;
  }
  if (status == HAL_OK) {
    status = queue_response(context, request_id);
  }
  jh_secure_zeroize(&context->dispatch_message,
                    sizeof(context->dispatch_message));
  jh_secure_zeroize(&context->dispatch_peer, sizeof(context->dispatch_peer));
  jh_secure_zeroize(&context->handler_response,
                    sizeof(context->handler_response));
  return status;
}

void consume_receive_prefix(jh_ble_commands_context_t *context, size_t length) {
  const size_t remaining = context->receive_length - length;
  const uint64_t last_counter = context->receive_peer.last_rx_counter;
  if (remaining > 0u) {
    memmove(context->receive_wire, &context->receive_wire[length], remaining);
  }
  jh_secure_zeroize(&context->receive_wire[remaining],
                    context->receive_length - remaining);
  context->receive_length = remaining;
  if (remaining == 0u) {
    memset(&context->receive_peer, 0, sizeof(context->receive_peer));
    context->partial_frame_started_ms = 0u;
  } else {
    context->receive_peer = context->peer;
    context->receive_peer.first_rx_counter = last_counter;
    context->receive_peer.last_rx_counter = last_counter;
    context->partial_frame_started_ms = hal_millis();
  }
}

hal_status_t handle_complete_frame(jh_ble_commands_context_t *context) {
  if (context->receive_length == 0u) {
    return HAL_EAGAIN;
  }
  size_t frame_length = 0u;
  hal_status_t status = hal_command_message_frame_size(
      context->receive_wire, context->receive_length, &frame_length);
  if (status == HAL_EAGAIN) {
    return status;
  }
  if (status != HAL_OK || frame_length > context->receive_length) {
    return close_corrupt_session(context,
                                 status == HAL_OK ? HAL_EPROTO : status);
  }

  status = hal_command_message_decode(context->receive_wire, frame_length,
                                      &context->scratch_message);
  if (status != HAL_OK) {
    return close_corrupt_session(context, status);
  }

  if (context->scratch_message.type == HAL_COMMAND_MESSAGE_REQUEST) {
    if (context->pending_response_length != 0u) {
      return HAL_EAGAIN;
    }
  } else if (context->received_ready) {
    return HAL_EAGAIN;
  }

  const hal_command_message_type_t type = context->scratch_message.type;
  hal_ble_commands_peer_info_t message_peer = context->receive_peer;
  hal_command_message_t message = context->scratch_message;
  jh_secure_zeroize(&context->scratch_message,
                    sizeof(context->scratch_message));
  consume_receive_prefix(context, frame_length);

  switch (type) {
  case HAL_COMMAND_MESSAGE_REQUEST:
    ++context->diagnostics.requests_received;
    context->dispatch_message = message;
    context->dispatch_peer = message_peer;
    jh_secure_zeroize(&message, sizeof(message));
    jh_secure_zeroize(&message_peer, sizeof(message_peer));
    return dispatch_request(context);

  case HAL_COMMAND_MESSAGE_RESPONSE:
    ++context->diagnostics.responses_received;
    break;

  case HAL_COMMAND_MESSAGE_EVENT:
    ++context->diagnostics.events_received;
    break;

  default:
    jh_secure_zeroize(&message, sizeof(message));
    jh_secure_zeroize(&message_peer, sizeof(message_peer));
    return close_corrupt_session(context, HAL_EPROTO);
  }

  context->received_message = message;
  context->received_peer = message_peer;
  context->received_ready = true;
  jh_secure_zeroize(&message, sizeof(message));
  jh_secure_zeroize(&message_peer, sizeof(message_peer));
  return HAL_OK;
}

hal_status_t append_stream_chunk(jh_ble_commands_context_t *context,
                                 const uint8_t *chunk, size_t length,
                                 const hal_ble_stream_payload_info_t &info) {
  if (chunk == nullptr || length == 0u ||
      info.generation != context->peer.stream_generation ||
      info.session_id != context->peer.session_id || info.counter == 0u) {
    return close_corrupt_session(context, HAL_EPROTO);
  }
  if (length > sizeof(context->receive_wire) - context->receive_length) {
    return close_corrupt_session(context, HAL_EOVERFLOW);
  }
  if (context->receive_length == 0u) {
    context->receive_peer = context->peer;
    context->receive_peer.first_rx_counter = info.counter;
    context->receive_peer.last_rx_counter = info.counter;
    context->partial_frame_started_ms = hal_millis();
  } else {
    if (info.counter != context->receive_peer.last_rx_counter + 1u) {
      return close_corrupt_session(context, HAL_EPROTO);
    }
    context->receive_peer.last_rx_counter = info.counter;
  }
  memcpy(&context->receive_wire[context->receive_length], chunk, length);
  context->receive_length += length;
  ++context->diagnostics.stream_chunks_received;
  return HAL_OK;
}

hal_status_t receive_one_chunk(jh_ble_commands_context_t *context) {
  uint8_t chunk[HAL_BLE_STREAM_MAX_PAYLOAD]{};
  size_t length = 0u;
  hal_ble_stream_payload_info_t info{};
  hal_status_t status = jh_ble_stream_receive_for_session(
      chunk, sizeof(chunk), &length, &info, context->peer.stream_generation,
      context->peer.session_id);
  if (status == HAL_EAUTH) {
    jh_secure_zeroize(chunk, sizeof(chunk));
    return resynchronize_after_session_mismatch(context);
  }
  if (status == HAL_EOVERFLOW || status == HAL_EPROTO) {
    jh_secure_zeroize(chunk, sizeof(chunk));
    return close_corrupt_session(context, status);
  }
  if (status != HAL_OK) {
    jh_secure_zeroize(chunk, sizeof(chunk));
    record_error(context, status);
    return status;
  }
  status = append_stream_chunk(context, chunk, length, info);
  jh_secure_zeroize(chunk, sizeof(chunk));
  return status;
}

hal_status_t check_partial_timeout(jh_ble_commands_context_t *context) {
  if (context->receive_length == 0u) {
    return HAL_OK;
  }
  if (!hal_millis_deadline_expired(context->partial_frame_started_ms,
                                   context->config.partial_frame_timeout_ms)) {
    return HAL_OK;
  }
  size_t frame_length = 0u;
  if (hal_command_message_frame_size(context->receive_wire,
                                     context->receive_length,
                                     &frame_length) != HAL_EAGAIN) {
    return HAL_OK;
  }
  ++context->diagnostics.partial_frame_timeouts;
  return close_corrupt_session(context, HAL_ETIMEOUT);
}

hal_status_t queue_outgoing(jh_ble_commands_context_t *context,
                            hal_command_message_type_t type,
                            uint32_t request_id, const char *name,
                            hal_command_encoding_t encoding,
                            const void *payload, size_t payload_length) {
  if (context->transmit_length != 0u ||
      context->pending_response_length != 0u) {
    return HAL_EBUSY;
  }
  size_t wire_length = 0u;
  const hal_status_t status = jh_command_adapter_prepare_message(
      &context->scratch_message, type, request_id, name, encoding, payload,
      payload_length, context->transmit_wire, sizeof(context->transmit_wire),
      &wire_length);
  jh_secure_zeroize(&context->scratch_message,
                    sizeof(context->scratch_message));
  if (status == HAL_OK) {
    context->transmit_length = wire_length;
    context->transmit_offset = 0u;
    context->transmit_type = type;
  } else {
    jh_secure_zeroize(context->transmit_wire, sizeof(context->transmit_wire));
  }
  return status;
}

} // namespace

hal_ble_commands_config_t hal_ble_commands_config_defaults(void) {
  hal_ble_commands_config_t config{};
  config.initial_request_id = 1u;
  config.partial_frame_timeout_ms = HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS;
  return config;
}

hal_status_t hal_ble_commands_create(const hal_ble_commands_config_t *config,
                                     hal_ble_commands_t *out_commands) {
  if (out_commands != nullptr) {
    *out_commands = nullptr;
  }
  if (config == nullptr || out_commands == nullptr ||
      config->initial_request_id == 0u) {
    return HAL_EINVAL;
  }

  hal_command_router_t router = config->router;
  hal_status_t status = HAL_OK;
  if (router == nullptr) {
    status = hal_command_router_default(&router);
  }
  size_t command_count = 0u;
  if (status == HAL_OK) {
    status = hal_command_router_count(router, &command_count);
  }
  if (status != HAL_OK) {
    return status;
  }

  hal_ble_stream_info_t stream{};
  status = hal_ble_stream_get_info(&stream);
  if (status != HAL_OK) {
    return status;
  }
  if (stream.state == HAL_BLE_STREAM_STATE_UNINITIALIZED) {
    return HAL_EUNINIT;
  }

  status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  if (s_context.allocated) {
    pool_unlock();
    return HAL_EBUSY;
  }
  if (jh_hal_mutex_create_once(&s_context.mutex) == nullptr) {
    pool_unlock();
    return HAL_ENOMEM;
  }

  clear_context(&s_context);
  s_context.config = *config;
  s_context.config.router = router;
  if (s_context.config.partial_frame_timeout_ms == 0u) {
    s_context.config.partial_frame_timeout_ms =
        HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS;
  }
  s_context.next_request_id = config->initial_request_id;
  s_context.allocated = true;
  void *handle = nullptr;
  status = jh_handle_allocate(&s_handle_pool, &s_context, &handle);
  if (status == HAL_OK) {
    *out_commands = reinterpret_cast<hal_ble_commands_t>(handle);
  } else {
    clear_context(&s_context);
  }
  pool_unlock();
  return status;
}

hal_status_t hal_ble_commands_destroy(hal_ble_commands_t commands) {
  jh_command_adapter_operation_t operation{};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_ble_commands_context_t *context =
      jh_command_adapter_operation_context<jh_ble_commands_context_t>(
          &operation);
  if (context->process_active || context->dispatch_active ||
      context->transmit_length != 0u ||
      context->pending_response_length != 0u || context->receive_length != 0u ||
      context->received_ready) {
    return finish_operation(&operation, HAL_EBUSY);
  }

  hal_ble_stream_info_t stream{};
  status = hal_ble_stream_get_info(&stream);
  if (status != HAL_OK) {
    return finish_operation(&operation, status);
  }
  if (stream.pending_rx != 0u || stream.pending_tx != 0u) {
    return finish_operation(&operation, HAL_EBUSY);
  }

  status = pool_lock();
  if (status != HAL_OK) {
    return finish_operation(&operation, status);
  }
  void *released = nullptr;
  status = jh_handle_begin_close(&s_handle_pool, commands, &released);
  pool_unlock();
  if (status == HAL_OK && released != nullptr) {
    status = HAL_EINTERNAL;
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_ble_commands_request_start(hal_ble_commands_t commands,
                                            const char *command,
                                            hal_command_encoding_t encoding,
                                            const void *arguments,
                                            size_t arguments_length,
                                            uint32_t *out_request_id) {
  if (out_request_id == nullptr) {
    return HAL_EINVAL;
  }
  *out_request_id = 0u;
  jh_command_adapter_operation_t operation{};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_ble_commands_context_t *context =
      jh_command_adapter_operation_context<jh_ble_commands_context_t>(
          &operation);
  status = synchronize_session(context);
  if (status != HAL_OK) {
    return finish_operation(&operation,
                            status == HAL_EAGAIN ? HAL_EAUTH : status);
  }

  const uint32_t request_id = context->next_request_id;
  status = queue_outgoing(context, HAL_COMMAND_MESSAGE_REQUEST, request_id,
                          command, encoding, arguments, arguments_length);
  if (status == HAL_OK) {
    ++context->diagnostics.requests_started;
    *out_request_id = request_id;
    context->next_request_id = jh_command_adapter_next_request_id(request_id);
  } else {
    record_error(context, status);
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_ble_commands_event_start(hal_ble_commands_t commands,
                                          const char *event,
                                          hal_command_encoding_t encoding,
                                          const void *payload,
                                          size_t payload_length) {
  jh_command_adapter_operation_t operation{};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_ble_commands_context_t *context =
      jh_command_adapter_operation_context<jh_ble_commands_context_t>(
          &operation);
  status = synchronize_session(context);
  if (status != HAL_OK) {
    return finish_operation(&operation,
                            status == HAL_EAGAIN ? HAL_EAUTH : status);
  }

  status = queue_outgoing(context, HAL_COMMAND_MESSAGE_EVENT, 0u, event,
                          encoding, payload, payload_length);
  if (status == HAL_OK) {
    ++context->diagnostics.events_started;
  } else {
    record_error(context, status);
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_ble_commands_process(hal_ble_commands_t commands) {
  jh_command_adapter_operation_t operation{};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_ble_commands_context_t *context =
      jh_command_adapter_operation_context<jh_ble_commands_context_t>(
          &operation);
  if (context->process_active) {
    return finish_operation(&operation, HAL_EBUSY);
  }
  context->process_active = true;
  ++context->diagnostics.process_calls;

  status = synchronize_session(context);
  if (status != HAL_OK) {
    context->process_active = false;
    return finish_operation(&operation,
                            status == HAL_EAGAIN ? HAL_EAGAIN : status);
  }
  status = check_partial_timeout(context);
  if (status != HAL_OK) {
    context->process_active = false;
    return finish_operation(&operation, status);
  }

  bool progressed = false;
  if (context->transmit_length != 0u ||
      context->pending_response_length != 0u) {
    status = send_one_chunk(context);
    if (status == HAL_OK) {
      progressed = true;
    } else if (status != HAL_EAGAIN) {
      context->process_active = false;
      return finish_operation(&operation, status);
    }
  }

  status = handle_complete_frame(context);
  if (status == HAL_OK) {
    progressed = true;
  } else if (status != HAL_EAGAIN) {
    context->process_active = false;
    return finish_operation(&operation, status);
  }

  if (status == HAL_EAGAIN && !context->received_ready &&
      context->pending_response_length == 0u) {
    status = receive_one_chunk(context);
    if (status == HAL_OK) {
      progressed = true;
      status = handle_complete_frame(context);
      if (status != HAL_OK && status != HAL_EAGAIN) {
        context->process_active = false;
        return finish_operation(&operation, status);
      }
    } else if (status != HAL_EAGAIN) {
      context->process_active = false;
      return finish_operation(&operation, status);
    }
  }

  context->process_active = false;
  return finish_operation(&operation, progressed ? HAL_OK : HAL_EAGAIN);
}

hal_status_t
hal_ble_commands_receive(hal_ble_commands_t commands,
                         hal_command_message_t *out_message,
                         hal_ble_commands_peer_info_t *out_peer_info) {
  if (out_message == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_message, 0, sizeof(*out_message));
  if (out_peer_info != nullptr) {
    memset(out_peer_info, 0, sizeof(*out_peer_info));
  }

  jh_command_adapter_operation_t operation{};
  const hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_ble_commands_context_t *context =
      jh_command_adapter_operation_context<jh_ble_commands_context_t>(
          &operation);
  if (!context->received_ready) {
    return finish_operation(&operation, HAL_EAGAIN);
  }
  *out_message = context->received_message;
  if (out_peer_info != nullptr) {
    *out_peer_info = context->received_peer;
  }
  jh_secure_zeroize(&context->received_message,
                    sizeof(context->received_message));
  jh_secure_zeroize(&context->received_peer, sizeof(context->received_peer));
  context->received_ready = false;
  return finish_operation(&operation, HAL_OK);
}

hal_status_t hal_ble_commands_get_info(hal_ble_commands_t commands,
                                       hal_ble_commands_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_info, 0, sizeof(*out_info));
  jh_command_adapter_operation_t operation{};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_ble_commands_context_t *context =
      jh_command_adapter_operation_context<jh_ble_commands_context_t>(
          &operation);
  hal_ble_stream_info_t stream{};
  status = hal_ble_stream_get_info(&stream);
  if (status == HAL_OK) {
    out_info->stream_state = stream.state;
    out_info->session_id = stream.state == HAL_BLE_STREAM_STATE_AUTHENTICATED
                               ? stream.session_id
                               : 0u;
    out_info->next_request_id = context->next_request_id;
    out_info->receive_buffered = context->receive_length;
    out_info->transmit_length = context->transmit_length;
    out_info->transmit_offset = context->transmit_offset;
    out_info->pending_response = context->pending_response_length != 0u;
    out_info->receive_ready = context->received_ready;
    out_info->process_active = context->process_active;
    out_info->dispatch_active = context->dispatch_active;
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_ble_commands_get_diagnostics(
    hal_ble_commands_t commands,
    hal_ble_commands_diagnostics_t *out_diagnostics) {
  if (out_diagnostics == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_diagnostics, 0, sizeof(*out_diagnostics));
  jh_command_adapter_operation_t operation{};
  const hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  *out_diagnostics =
      jh_command_adapter_operation_context<jh_ble_commands_context_t>(
          &operation)
          ->diagnostics;
  return finish_operation(&operation, HAL_OK);
}

#if HAL_TARGET_IS_MOCK
/* Test-only: force the pool mutex and the context mutex through a real
 * destroy so Helgrind/DRD can observe the teardown path, then mark the
 * pool uninitialized so the next adapter operation recreates them from
 * scratch. Firmware never calls this. Call only when no other thread is
 * using the adapter. */
void hal_mock_ble_commands_full_reset(void) {
  if (s_context.mutex != nullptr) {
    hal_mutex_destroy(s_context.mutex);
  }
  memset(&s_context, 0, sizeof(s_context));
  if (s_pool_mutex != nullptr) {
    hal_mutex_destroy(s_pool_mutex);
    s_pool_mutex = nullptr;
  }
  s_pool_initialized = false;
}
#endif /* HAL_TARGET_IS_MOCK */

#endif /* HAL_ENABLE_BLE_COMMANDS */

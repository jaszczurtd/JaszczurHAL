#include "hal/radio/hal_lora_commands.h"

#ifdef HAL_ENABLE_LORA_COMMANDS

#include "hal/commands/jh_command_adapter_internal.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/system/hal_sync.h"

#include <string.h>

#define JH_LORA_COMMANDS_HANDLE_KIND 12u

typedef struct {
  hal_lora_commands_config_t config;
  hal_mutex_t mutex;
  hal_lora_commands_diagnostics_t diagnostics;
  hal_lora_link_message_info_t last_received;
  hal_command_message_t scratch_message;
  hal_command_message_t dispatch_message;
  hal_lora_link_message_info_t dispatch_info;
  hal_command_response_t handler_response;
  uint8_t receive_wire[HAL_LORA_LINK_MAX_MESSAGE_SIZE];
  uint8_t transmit_wire[HAL_LORA_LINK_MAX_MESSAGE_SIZE];
  size_t pending_response_length;
  uint16_t pending_response_destination;
  uint32_t next_request_id;
  hal_command_message_t received_message;
  hal_lora_link_message_info_t received_info;
  bool pending_response;
  bool received_ready;
  bool process_active;
  bool dispatch_active;
  bool allocated;
} jh_lora_commands_context_t;

static jh_lora_commands_context_t s_contexts[HAL_LORA_LINK_MAX_INSTANCES] = {};
static jh_handle_slot_t s_handle_slots[HAL_LORA_LINK_MAX_INSTANCES] = {};
static jh_handle_pool_t s_handle_pool = {};
static hal_mutex_t s_pool_mutex = NULL;
static bool s_pool_initialized = false;

static hal_status_t pool_lock(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    const hal_status_t status = jh_handle_pool_init(
        &s_handle_pool, s_handle_slots, HAL_LORA_LINK_MAX_INSTANCES,
        JH_LORA_COMMANDS_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static void clear_context(void *token) {
  jh_lora_commands_context_t *context =
      static_cast<jh_lora_commands_context_t *>(token);
  hal_mutex_t mutex = context->mutex;
  memset(context, 0, sizeof(*context));
  context->mutex = mutex;
  context->diagnostics.last_dispatch_status = HAL_NONE;
  context->diagnostics.last_error = HAL_NONE;
}

static const jh_command_adapter_context_access_t s_context_access = {
    &s_handle_pool,
    pool_lock,
    pool_unlock,
    jh_command_adapter_context_mutex<jh_lora_commands_context_t>,
    jh_command_adapter_context_allocated<jh_lora_commands_context_t>,
    clear_context};

static hal_status_t context_lock(hal_lora_commands_t commands,
                                 jh_command_adapter_operation_t *operation) {
  return jh_command_adapter_context_lock(&s_context_access, commands,
                                         operation);
}

static hal_status_t finish_operation(jh_command_adapter_operation_t *operation,
                                     hal_status_t status) {
  return jh_command_adapter_operation_finish(operation, status);
}

static void record_error(jh_lora_commands_context_t *context,
                         hal_status_t status) {
  if (status != HAL_OK && status != HAL_EAGAIN && status != HAL_EBUSY &&
      status != HAL_IGNORED) {
    context->diagnostics.last_error = status;
  }
}

static bool process_status_is_hard(hal_status_t status) {
  return status != HAL_OK && status != HAL_EAGAIN && status != HAL_EBUSY &&
         status != HAL_IGNORED;
}

static hal_status_t select_process_status(hal_status_t link_status,
                                          hal_status_t operation_status,
                                          bool progressed) {
  if (process_status_is_hard(link_status)) {
    return link_status;
  }
  if (process_status_is_hard(operation_status)) {
    return operation_status;
  }
  if (operation_status == HAL_IGNORED || link_status == HAL_IGNORED) {
    return HAL_IGNORED;
  }
  return progressed ? HAL_OK : HAL_EAGAIN;
}

static hal_status_t finish_process(jh_command_adapter_operation_t *operation,
                                   hal_status_t status) {
  jh_command_adapter_operation_context<jh_lora_commands_context_t>(operation)
      ->process_active = false;
  return finish_operation(operation, status);
}

static hal_status_t prepare_outgoing_message(
    jh_lora_commands_context_t *context, hal_command_message_type_t type,
    uint32_t request_id, const char *name, hal_command_encoding_t encoding,
    const void *payload, size_t payload_length, size_t *out_wire_length) {
  return jh_command_adapter_prepare_message(
      &context->scratch_message, type, request_id, name, encoding, payload,
      payload_length, context->transmit_wire, sizeof(context->transmit_wire),
      out_wire_length);
}

static hal_status_t try_pending_response(jh_lora_commands_context_t *context) {
  const hal_status_t status = hal_lora_link_send_start(
      context->config.link, context->pending_response_destination,
      context->config.port, context->transmit_wire,
      context->pending_response_length, context->config.acknowledged);
  if (status == HAL_OK) {
    ++context->diagnostics.responses_sent;
    memset(context->transmit_wire, 0, context->pending_response_length);
    context->pending_response_length = 0u;
    context->pending_response_destination = HAL_LORA_LINK_ADDRESS_NONE;
    context->pending_response = false;
    return HAL_OK;
  }
  if (status == HAL_EBUSY || status == HAL_EAGAIN) {
    ++context->diagnostics.pending_response_retries;
    return status;
  }
  ++context->diagnostics.response_send_failures;
  ++context->diagnostics.dropped_messages;
  context->pending_response_length = 0u;
  context->pending_response_destination = HAL_LORA_LINK_ADDRESS_NONE;
  context->pending_response = false;
  record_error(context, status);
  return status;
}

static hal_status_t
encode_dispatch_response(jh_lora_commands_context_t *context,
                         uint32_t request_id, uint16_t destination) {
  size_t wire_length = 0u;
  const hal_status_t status = jh_command_adapter_encode_response(
      &context->handler_response, request_id, &context->scratch_message,
      context->transmit_wire, sizeof(context->transmit_wire), &wire_length);
  if (status != HAL_OK) {
    ++context->diagnostics.response_send_failures;
    ++context->diagnostics.dropped_messages;
    record_error(context, status);
    return status;
  }

  context->pending_response_length = wire_length;
  context->pending_response_destination = destination;
  context->pending_response = true;
  return HAL_OK;
}

static hal_status_t dispatch_request(jh_lora_commands_context_t *context) {
  hal_command_request_t request = {};
  request.source = HAL_COMMAND_SOURCE_LORA_LINK;
  request.encoding = context->dispatch_message.encoding;
  request.command = context->dispatch_message.name;
  request.arguments = context->dispatch_message.payload_length == 0u
                          ? NULL
                          : context->dispatch_message.payload;
  request.arguments_length = context->dispatch_message.payload_length;
  request.request_id = context->dispatch_message.request_id;
  request.peer_id = context->dispatch_info.source;
  request.session_id = context->dispatch_info.session_id;
  request.security_flags =
      context->dispatch_info.encrypted ? HAL_COMMAND_SECURITY_ALL : 0u;
  request.source_context = &context->dispatch_info;

  const uint32_t request_id = request.request_id;
  const uint16_t source = context->dispatch_info.source;
  const auto router = context->config.router;
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
  const hal_status_t status =
      encode_dispatch_response(context, request_id, source);
  memset(&context->dispatch_message, 0, sizeof(context->dispatch_message));
  memset(&context->dispatch_info, 0, sizeof(context->dispatch_info));
  memset(&context->handler_response, 0, sizeof(context->handler_response));
  return status;
}

static hal_status_t handle_received_message(jh_lora_commands_context_t *context,
                                            size_t wire_length) {
  hal_status_t status = hal_command_message_decode(
      context->receive_wire, wire_length, &context->scratch_message);
  if (status != HAL_OK) {
    ++context->diagnostics.protocol_errors;
    ++context->diagnostics.dropped_messages;
    record_error(context, status);
    return status;
  }

  switch (context->scratch_message.type) {
  case HAL_COMMAND_MESSAGE_REQUEST:
    ++context->diagnostics.requests_received;
    context->dispatch_message = context->scratch_message;
    context->dispatch_info = context->last_received;
    status = dispatch_request(context);
    if (status != HAL_OK) {
      return status;
    }
    status = try_pending_response(context);
    return status == HAL_EBUSY || status == HAL_EAGAIN ? HAL_OK : status;

  case HAL_COMMAND_MESSAGE_RESPONSE:
    ++context->diagnostics.responses_received;
    break;

  case HAL_COMMAND_MESSAGE_EVENT:
    ++context->diagnostics.events_received;
    break;

  default:
    ++context->diagnostics.protocol_errors;
    ++context->diagnostics.dropped_messages;
    return HAL_EPROTO;
  }

  context->received_message = context->scratch_message;
  context->received_info = context->last_received;
  context->received_ready = true;
  return HAL_OK;
}

hal_lora_commands_config_t
hal_lora_commands_config_defaults(hal_lora_link_t link, uint8_t port) {
  hal_lora_commands_config_t config = {};
  config.link = link;
  config.router = NULL;
  config.port = port;
  config.acknowledged = true;
  config.initial_request_id = 1u;
  return config;
}

hal_status_t hal_lora_commands_create(const hal_lora_commands_config_t *config,
                                      hal_lora_commands_t *out_commands) {
  if (out_commands != NULL) {
    *out_commands = NULL;
  }
  if (config == NULL || out_commands == NULL || config->link == NULL ||
      config->initial_request_id == 0u ||
      HAL_LORA_LINK_MAX_MESSAGE_SIZE < HAL_COMMAND_WIRE_HEADER_SIZE) {
    return HAL_EINVAL;
  }

  hal_command_router_t router = config->router;
  hal_status_t status = HAL_OK;
  if (router == NULL) {
    status = hal_command_router_default(&router);
  }
  size_t command_count = 0u;
  if (status == HAL_OK) {
    status = hal_command_router_count(router, &command_count);
  }
  if (status != HAL_OK) {
    return status;
  }

  hal_lora_link_state_t link_state = HAL_LORA_LINK_STATE_ERROR;
  status = hal_lora_link_get_state(config->link, &link_state);
  if (status != HAL_OK) {
    return status;
  }
  if (link_state != HAL_LORA_LINK_STATE_RECEIVING) {
    return HAL_EBUSY;
  }

  status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context = NULL;
  for (size_t index = 0u; index < HAL_LORA_LINK_MAX_INSTANCES; ++index) {
    if (s_contexts[index].allocated &&
        s_contexts[index].config.link == config->link) {
      pool_unlock();
      return HAL_EBUSY;
    }
    if (context == NULL && !s_contexts[index].allocated) {
      context = &s_contexts[index];
    }
  }
  if (context == NULL) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  if (jh_hal_mutex_create_once(&context->mutex) == NULL) {
    pool_unlock();
    return HAL_ENOMEM;
  }

  clear_context(context);
  context->config = *config;
  context->config.router = router;
  context->next_request_id = config->initial_request_id;
  context->allocated = true;
  void *handle = NULL;
  status = jh_handle_allocate(&s_handle_pool, context, &handle);
  if (status == HAL_OK) {
    *out_commands = reinterpret_cast<hal_lora_commands_t>(handle);
  } else {
    clear_context(context);
  }
  pool_unlock();
  return status;
}

hal_status_t hal_lora_commands_destroy(hal_lora_commands_t commands) {
  jh_command_adapter_operation_t operation = {};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context =
      jh_command_adapter_operation_context<jh_lora_commands_context_t>(
          &operation);
  if (context->process_active || context->dispatch_active ||
      context->pending_response || context->received_ready) {
    return finish_operation(&operation, HAL_EBUSY);
  }

  hal_lora_link_state_t link_state = HAL_LORA_LINK_STATE_ERROR;
  status = hal_lora_link_get_state(context->config.link, &link_state);
  if (status != HAL_OK) {
    return finish_operation(&operation, status);
  }
  if (link_state != HAL_LORA_LINK_STATE_RECEIVING) {
    return finish_operation(&operation, HAL_EBUSY);
  }

  status = pool_lock();
  if (status != HAL_OK) {
    return finish_operation(&operation, status);
  }
  void *released = NULL;
  status = jh_handle_begin_close(&s_handle_pool, commands, &released);
  pool_unlock();
  if (status == HAL_OK && released != NULL) {
    status = HAL_EINTERNAL;
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_lora_commands_request_start(
    hal_lora_commands_t commands, uint16_t destination, const char *command,
    hal_command_encoding_t encoding, const void *arguments,
    size_t arguments_length, uint32_t *out_request_id) {
  if (out_request_id == NULL) {
    return HAL_EINVAL;
  }
  *out_request_id = 0u;
  if (destination == HAL_LORA_LINK_ADDRESS_BROADCAST) {
    return HAL_EINVAL;
  }
  jh_command_adapter_operation_t operation = {};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context =
      jh_command_adapter_operation_context<jh_lora_commands_context_t>(
          &operation);
  if (context->pending_response) {
    return finish_operation(&operation, HAL_EBUSY);
  }

  const uint32_t request_id = context->next_request_id;
  size_t wire_length = 0u;
  status = prepare_outgoing_message(context, HAL_COMMAND_MESSAGE_REQUEST,
                                    request_id, command, encoding, arguments,
                                    arguments_length, &wire_length);
  if (status == HAL_OK) {
    status = hal_lora_link_send_start(
        context->config.link, destination, context->config.port,
        context->transmit_wire, wire_length, context->config.acknowledged);
  }
  if (status == HAL_OK) {
    ++context->diagnostics.requests_sent;
    *out_request_id = request_id;
    context->next_request_id = jh_command_adapter_next_request_id(request_id);
    memset(context->transmit_wire, 0, wire_length);
  } else {
    record_error(context, status);
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_lora_commands_event_start(hal_lora_commands_t commands,
                                           uint16_t destination,
                                           const char *event,
                                           hal_command_encoding_t encoding,
                                           const void *payload,
                                           size_t payload_length) {
  jh_command_adapter_operation_t operation = {};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context =
      jh_command_adapter_operation_context<jh_lora_commands_context_t>(
          &operation);
  if (context->pending_response) {
    return finish_operation(&operation, HAL_EBUSY);
  }

  size_t wire_length = 0u;
  status =
      prepare_outgoing_message(context, HAL_COMMAND_MESSAGE_EVENT, 0u, event,
                               encoding, payload, payload_length, &wire_length);
  if (status == HAL_OK) {
    const bool acknowledged = destination == HAL_LORA_LINK_ADDRESS_BROADCAST
                                  ? false
                                  : context->config.acknowledged;
    status = hal_lora_link_send_start(
        context->config.link, destination, context->config.port,
        context->transmit_wire, wire_length, acknowledged);
  }
  if (status == HAL_OK) {
    ++context->diagnostics.events_sent;
    memset(context->transmit_wire, 0, wire_length);
  } else {
    record_error(context, status);
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_lora_commands_process(hal_lora_commands_t commands) {
  jh_command_adapter_operation_t operation = {};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context =
      jh_command_adapter_operation_context<jh_lora_commands_context_t>(
          &operation);
  if (context->process_active) {
    return finish_operation(&operation, HAL_EBUSY);
  }
  context->process_active = true;
  ++context->diagnostics.process_calls;

  const hal_status_t link_status = hal_lora_link_process(context->config.link);
  record_error(context, link_status);
  bool progressed = link_status == HAL_OK;

  if (context->pending_response) {
    const hal_status_t pending_status = try_pending_response(context);
    if (pending_status == HAL_OK) {
      progressed = true;
    }
    if (pending_status != HAL_OK && pending_status != HAL_EBUSY &&
        pending_status != HAL_EAGAIN) {
      return finish_process(
          &operation,
          select_process_status(link_status, pending_status, progressed));
    }
    if (context->pending_response) {
      return finish_process(
          &operation,
          select_process_status(link_status, HAL_EAGAIN, progressed));
    }
  }

  if (context->received_ready) {
    return finish_process(
        &operation, select_process_status(link_status, HAL_EAGAIN, progressed));
  }

  size_t wire_length = 0u;
  hal_lora_link_message_info_t link_info = {};
  status = hal_lora_link_receive(context->config.link, context->receive_wire,
                                 sizeof(context->receive_wire), &wire_length,
                                 &link_info);
  if (status == HAL_EAGAIN) {
    return finish_process(
        &operation, select_process_status(link_status, status, progressed));
  }
  if (status != HAL_OK) {
    ++context->diagnostics.dropped_messages;
    record_error(context, status);
    return finish_process(
        &operation, select_process_status(link_status, status, progressed));
  }
  progressed = true;

  context->last_received = link_info;
  if (link_info.port != context->config.port) {
    ++context->diagnostics.wrong_port_messages;
    ++context->diagnostics.dropped_messages;
    return finish_process(
        &operation,
        select_process_status(link_status, HAL_IGNORED, progressed));
  }

  status = handle_received_message(context, wire_length);
  return finish_process(&operation,
                        select_process_status(link_status, status, progressed));
}

hal_status_t
hal_lora_commands_receive(hal_lora_commands_t commands,
                          hal_command_message_t *out_message,
                          hal_lora_link_message_info_t *out_link_info) {
  if (out_message == NULL) {
    return HAL_EINVAL;
  }
  memset(out_message, 0, sizeof(*out_message));
  if (out_link_info != NULL) {
    memset(out_link_info, 0, sizeof(*out_link_info));
  }

  jh_command_adapter_operation_t operation = {};
  const hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context =
      jh_command_adapter_operation_context<jh_lora_commands_context_t>(
          &operation);
  if (!context->received_ready) {
    return finish_operation(&operation, HAL_EAGAIN);
  }
  *out_message = context->received_message;
  if (out_link_info != NULL) {
    *out_link_info = context->received_info;
  }
  memset(&context->received_message, 0, sizeof(context->received_message));
  memset(&context->received_info, 0, sizeof(context->received_info));
  context->received_ready = false;
  return finish_operation(&operation, HAL_OK);
}

hal_status_t hal_lora_commands_get_info(hal_lora_commands_t commands,
                                        hal_lora_commands_info_t *out_info) {
  if (out_info == NULL) {
    return HAL_EINVAL;
  }
  memset(out_info, 0, sizeof(*out_info));
  jh_command_adapter_operation_t operation = {};
  hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context =
      jh_command_adapter_operation_context<jh_lora_commands_context_t>(
          &operation);

  hal_lora_commands_info_t info = {};
  status = hal_lora_link_get_state(context->config.link, &info.link_state);
  if (status == HAL_OK) {
    status =
        hal_lora_link_get_send_status(context->config.link, &info.link_send);
  }
  if (status == HAL_OK) {
    info.last_received = context->last_received;
    info.next_request_id = context->next_request_id;
    info.pending_response_destination = context->pending_response_destination;
    info.port = context->config.port;
    info.acknowledged = context->config.acknowledged;
    info.pending_response = context->pending_response;
    info.receive_ready = context->received_ready;
    info.process_active = context->process_active;
    info.dispatch_active = context->dispatch_active;
    *out_info = info;
  }
  return finish_operation(&operation, status);
}

hal_status_t hal_lora_commands_get_diagnostics(
    hal_lora_commands_t commands,
    hal_lora_commands_diagnostics_t *out_diagnostics) {
  if (out_diagnostics == NULL) {
    return HAL_EINVAL;
  }
  memset(out_diagnostics, 0, sizeof(*out_diagnostics));
  jh_command_adapter_operation_t operation = {};
  const hal_status_t status = context_lock(commands, &operation);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_commands_context_t *context =
      jh_command_adapter_operation_context<jh_lora_commands_context_t>(
          &operation);
  *out_diagnostics = context->diagnostics;
  return finish_operation(&operation, HAL_OK);
}

#endif /* HAL_ENABLE_LORA_COMMANDS */

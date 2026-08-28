#include "hal/serial/hal_serial_commands.h"

#ifdef HAL_ENABLE_SERIAL_COMMANDS

#include "hal/serial/hal_serial_frame.h"

#include <stdio.h>
#include <string.h>

namespace {

hal_status_t validate_payload(const char *payload, size_t length) {
  if (payload == nullptr || length == 0u) {
    return HAL_EPROTO;
  }
  if (length > HAL_SERIAL_FRAME_PAYLOAD_MAX) {
    return HAL_EOVERFLOW;
  }
  for (size_t index = 0u; index < length; ++index) {
    const char value = payload[index];
    if (value == '\0' || value == '*' || value == '\r' || value == '\n') {
      return HAL_EPROTO;
    }
  }
  return HAL_OK;
}

hal_status_t emit_payload(hal_serial_commands_t *commands, const char *payload,
                          size_t length) {
  hal_status_t status = validate_payload(payload, length);
  if (status != HAL_OK) {
    return status;
  }
  char copied[HAL_SERIAL_FRAME_PAYLOAD_MAX + 1u] = {};
  memcpy(copied, payload, length);
  copied[length] = '\0';
  status = hal_serial_session_println_ex(commands->config.session, copied);
  memset(copied, 0, sizeof(copied));
  return status;
}

hal_status_t emit_default_status(hal_serial_commands_t *commands,
                                 hal_status_t status) {
  char payload[48] = {};
  int length = 0;
  if (status == HAL_OK) {
    length = snprintf(payload, sizeof(payload), "OK");
  } else {
    length = snprintf(payload, sizeof(payload), "ERR %s",
                      hal_status_to_string(status));
  }
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(payload)) {
    return HAL_EINTERNAL;
  }
  return emit_payload(commands, payload, static_cast<size_t>(length));
}

hal_status_t emit_session_vocabulary_or_status(hal_serial_commands_t *commands,
                                               const char *payload,
                                               hal_status_t fallback_status) {
  if (payload != nullptr && payload[0] != '\0') {
    return emit_payload(commands, payload, strlen(payload));
  }
  return emit_default_status(commands, fallback_status);
}

bool prefix_matches(const hal_serial_commands_t *commands, const char *line) {
  const char *prefix = commands->config.command_prefix;
  return prefix == nullptr || prefix[0] == '\0' ||
         strncmp(line, prefix, strlen(prefix)) == 0;
}

hal_status_t build_request(hal_serial_commands_t *commands, const char *line,
                           hal_command_request_t *request,
                           char command[HAL_COMMAND_ROUTER_NAME_MAX]) {
  if (line == nullptr || request == nullptr || command == nullptr) {
    return HAL_EINVAL;
  }

  const char *name = line;
  while (*name == ' ' || *name == '\t') {
    ++name;
  }
  const char *name_end = name;
  while (*name_end != '\0' && *name_end != ' ' && *name_end != '\t') {
    ++name_end;
  }
  const size_t name_length = static_cast<size_t>(name_end - name);
  if (name_length == 0u || name_length >= HAL_COMMAND_ROUTER_NAME_MAX) {
    return HAL_EINVAL;
  }
  memcpy(command, name, name_length);
  command[name_length] = '\0';

  const char *arguments = name_end;
  while (*arguments == ' ' || *arguments == '\t') {
    ++arguments;
  }
  if (*arguments == '\0' && *name_end != '\0') {
    arguments = name_end;
  }

  uint16_t sequence = 0u;
  hal_status_t status = hal_serial_session_current_request_seq(
      commands->config.session, &sequence);
  if (status != HAL_OK) {
    return status;
  }

  memset(request, 0, sizeof(*request));
  request->source = HAL_COMMAND_SOURCE_SERIAL_SESSION;
  request->encoding = commands->config.encoding;
  request->command = command;
  request->arguments = *arguments == '\0'
                           ? nullptr
                           : reinterpret_cast<const uint8_t *>(arguments);
  request->arguments_length = strlen(arguments);
  request->request_id = sequence;
  request->peer_id = commands->config.peer_id;
  request->session_id = hal_serial_session_id(commands->config.session);
  request->security_flags =
      hal_serial_session_is_authenticated(commands->config.session)
          ? HAL_COMMAND_SECURITY_AUTHENTICATED
          : 0u;
  request->source_context = commands->config.session;
  return HAL_OK;
}

hal_status_t format_response(hal_serial_commands_t *commands,
                             const hal_command_request_t *request,
                             const hal_command_response_t *response) {
  const bool textual = response->encoding == HAL_COMMAND_ENCODING_TEXT ||
                       response->encoding == HAL_COMMAND_ENCODING_JSON;
  const bool body_length_valid = response->body_len <= sizeof(response->body);
  if (!response->overflow && body_length_valid && textual &&
      response->body_len > 0u) {
    const hal_status_t emit_status =
        emit_payload(commands, response->body, response->body_len);
    if (emit_status != HAL_OK) {
      (void)emit_default_status(commands, emit_status);
    }
    return emit_status;
  }

  hal_status_t effective_status = response->status;
  hal_status_t adapter_status = HAL_OK;
  if (response->overflow || !body_length_valid) {
    effective_status = HAL_EOVERFLOW;
    adapter_status = HAL_EOVERFLOW;
  } else if (!textual && response->body_len > 0u) {
    effective_status = HAL_EUNSUPPORTED;
    adapter_status = HAL_EUNSUPPORTED;
  }

  if (commands->config.formatter == nullptr) {
    const hal_status_t emit_status =
        emit_default_status(commands, effective_status);
    return emit_status == HAL_OK ? adapter_status : emit_status;
  }

  char payload[HAL_SERIAL_FRAME_PAYLOAD_MAX + 1u] = {};
  size_t length = 0u;
  const hal_status_t format_status = commands->config.formatter(
      request, response, payload, HAL_SERIAL_FRAME_PAYLOAD_MAX, &length,
      commands->config.formatter_user);
  if (format_status != HAL_OK) {
    (void)emit_default_status(commands, format_status);
    memset(payload, 0, sizeof(payload));
    return format_status;
  }
  const hal_status_t emit_status = emit_payload(commands, payload, length);
  if (emit_status != HAL_OK) {
    (void)emit_default_status(commands, emit_status);
  }
  memset(payload, 0, sizeof(payload));
  return emit_status == HAL_OK ? adapter_status : emit_status;
}

hal_status_t process_unknown_payload(hal_serial_commands_t *commands,
                                     const char *line) {
  if (!prefix_matches(commands, line)) {
    if (commands->config.fallback != nullptr) {
      commands->config.fallback(line, commands->config.fallback_user);
      return HAL_IGNORED;
    }
    const char *unknown =
        HAL_SERIAL_SESSION_VOCAB(commands->config.session, reply_unknown_cmd);
    const hal_status_t emit_status =
        emit_session_vocabulary_or_status(commands, unknown, HAL_ENOENT);
    return emit_status == HAL_OK ? HAL_ENOENT : emit_status;
  }

  char command[HAL_COMMAND_ROUTER_NAME_MAX] = {};
  hal_command_request_t request = {};
  hal_status_t status = build_request(commands, line, &request, command);
  if (status != HAL_OK) {
    const hal_status_t emit_status = emit_default_status(commands, status);
    return emit_status == HAL_OK ? status : emit_status;
  }

  if (!hal_serial_session_is_active(commands->config.session) &&
      (commands->config.allow_inactive == nullptr ||
       !commands->config.allow_inactive(
           &request, commands->config.allow_inactive_user))) {
    const char *not_ready = HAL_SERIAL_SESSION_VOCAB(
        commands->config.session, reply_not_ready_hello_required);
    const hal_status_t emit_status =
        emit_session_vocabulary_or_status(commands, not_ready, HAL_ESTATE);
    memset(command, 0, sizeof(command));
    return emit_status == HAL_OK ? HAL_ESTATE : emit_status;
  }

  hal_command_response_t response = {};
  const hal_status_t dispatch_status =
      hal_command_router_dispatch(commands->config.router, &request, &response);
  const hal_status_t response_status =
      format_response(commands, &request, &response);
  memset(command, 0, sizeof(command));
  memset(&response, 0, sizeof(response));
  return response_status == HAL_OK ? dispatch_status : response_status;
}

void handle_unknown_payload(const char *line, void *user) {
  auto *commands = static_cast<hal_serial_commands_t *>(user);
  if (commands == nullptr || !commands->initialized || line == nullptr ||
      commands->dispatch_active) {
    return;
  }

  commands->dispatch_active = true;
  const hal_status_t status = process_unknown_payload(commands, line);
  commands->dispatch_active = false;
  commands->last_status = status;
}

} // namespace

hal_serial_commands_config_t
hal_serial_commands_config_defaults(hal_serial_session_t *session) {
  hal_serial_commands_config_t config = {};
  config.session = session;
  config.encoding = HAL_COMMAND_ENCODING_TEXT;
  return config;
}

hal_status_t
hal_serial_commands_init(hal_serial_commands_t *commands,
                         const hal_serial_commands_config_t *config) {
  if (commands == nullptr || config == nullptr || config->session == nullptr ||
      (config->encoding != HAL_COMMAND_ENCODING_TEXT &&
       config->encoding != HAL_COMMAND_ENCODING_JSON)) {
    return HAL_EINVAL;
  }
  if (commands->initialized) {
    return HAL_EBUSY;
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

  hal_serial_commands_t prepared = {};
  prepared.config = *config;
  prepared.config.router = router;
  prepared.last_status = HAL_NONE;
  prepared.initialized = true;
  *commands = prepared;
  status = hal_serial_session_attach_unknown_handler(
      config->session, handle_unknown_payload, commands);
  if (status != HAL_OK) {
    if (status == HAL_EEXIST) {
      (void)hal_serial_session_detach_unknown_handler(
          config->session, handle_unknown_payload, commands);
    }
    memset(commands, 0, sizeof(*commands));
  }
  return status;
}

hal_status_t hal_serial_commands_deinit(hal_serial_commands_t *commands) {
  if (commands == nullptr) {
    return HAL_EINVAL;
  }
  if (!commands->initialized) {
    return HAL_EUNINIT;
  }
  if (commands->dispatch_active) {
    return HAL_EBUSY;
  }
  const hal_status_t status = hal_serial_session_detach_unknown_handler(
      commands->config.session, handle_unknown_payload, commands);
  if (status == HAL_OK) {
    memset(commands, 0, sizeof(*commands));
  }
  return status;
}

hal_status_t
hal_serial_commands_get_last_status(const hal_serial_commands_t *commands,
                                    hal_status_t *out_status) {
  if (commands == nullptr || out_status == nullptr) {
    return HAL_EINVAL;
  }
  *out_status = HAL_NONE;
  if (!commands->initialized) {
    return HAL_EUNINIT;
  }
  *out_status = commands->last_status;
  return HAL_OK;
}

#endif /* HAL_ENABLE_SERIAL_COMMANDS */

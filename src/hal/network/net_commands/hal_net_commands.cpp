/** @file Target-neutral network command dispatcher service. */
#include "hal/network/net_commands/hal_net_commands.h"

#ifdef HAL_ENABLE_NET_COMMANDS

#include "hal/commands/jh_command_router_internal.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *args_text;
  const cJSON *json_root;
  const cJSON *json_args;
  const hal_http_request_t *http_request;
  hal_websocket_client_t websocket_client;
} net_command_source_context_t;

static bool is_valid_format(hal_net_commands_format_t format) {
  return format == HAL_NET_COMMANDS_FORMAT_TEXT ||
         format == HAL_NET_COMMANDS_FORMAT_JSON ||
         format == HAL_NET_COMMANDS_FORMAT_AUTO;
}

static hal_status_t
response_write_default_json(hal_net_command_response_t *response) {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return HAL_ENOMEM;
  }

  cJSON_AddBoolToObject(root, "ok", response->status == HAL_OK);
  cJSON_AddStringToObject(root, "status",
                          hal_status_to_string(response->status));
  if (response->message) {
    cJSON_AddStringToObject(root, "message", response->message);
  }

  hal_status_t status = hal_net_command_response_write_json(response, root);
  cJSON_Delete(root);
  return status;
}

static hal_status_t
response_write_default_text(hal_net_command_response_t *response) {
  char line[96];
  int written = snprintf(line, sizeof(line), "%s %s%s%s\n",
                         response->status == HAL_OK ? "OK" : "ERR",
                         hal_status_to_string(response->status),
                         response->message ? " " : "",
                         response->message ? response->message : "");
  if (written < 0 || (size_t)written >= sizeof(line)) {
    return HAL_EOVERFLOW;
  }
  return hal_net_command_response_write(response, line, (size_t)written);
}

static hal_status_t finalize_response(hal_net_command_response_t *response,
                                      hal_status_t status, const char *message,
                                      hal_net_commands_format_t format) {
  if (!response) {
    return HAL_EINVAL;
  }

  if (status != HAL_OK && response->status == HAL_OK) {
    hal_net_command_response_set_status(response, status, message);
  } else if (response->message == NULL && message != NULL) {
    response->message = message;
  }

  if (response->body_len > 0u) {
    return response->status;
  }

  if (format == HAL_NET_COMMANDS_FORMAT_JSON) {
    hal_status_t write_status = response_write_default_json(response);
    return write_status == HAL_OK ? response->status : write_status;
  }
  hal_status_t write_status = response_write_default_text(response);
  return write_status == HAL_OK ? response->status : write_status;
}

static const char *skip_ws(const char *text) {
  while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
    ++text;
  }
  return text;
}

static void trim_end(char *text) {
  size_t len = strlen(text);
  while (len > 0u) {
    char c = text[len - 1u];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      break;
    }
    text[--len] = '\0';
  }
}

static hal_status_t invoke_net_handler(const void *callback_storage,
                                       size_t callback_size,
                                       const hal_command_request_t *request,
                                       hal_command_response_t *response,
                                       void *user) {
  if (callback_storage == NULL ||
      callback_size != sizeof(hal_net_command_handler_t) || request == NULL ||
      response == NULL || request->source_context == NULL) {
    return HAL_EINTERNAL;
  }

  hal_net_command_handler_t handler = NULL;
  memcpy(reinterpret_cast<void *>(&handler), callback_storage, sizeof(handler));
  if (handler == NULL) {
    return HAL_EINTERNAL;
  }

  const net_command_source_context_t *context =
      (const net_command_source_context_t *)request->source_context;
  hal_net_command_request_t net_request = {};
  net_request.source = (hal_net_commands_source_t)request->source;
  net_request.command = request->command;
  net_request.args_text = context->args_text;
  net_request.json_root = context->json_root;
  net_request.json_args = context->json_args;
  net_request.http_request = context->http_request;
  net_request.websocket_client = context->websocket_client;
  return handler(&net_request, response, user);
}

static hal_status_t dispatch_command(const hal_command_request_t *request,
                                     hal_net_command_response_t *response,
                                     hal_net_commands_format_t format) {
  hal_command_router_t router = NULL;
  hal_status_t status = hal_command_router_default(&router);
  if (status == HAL_OK) {
    status = hal_command_router_dispatch(router, request, response);
  }
  const char *message = response->message != NULL
                            ? response->message
                            : hal_status_to_string(status);
  return finalize_response(response, status, message, format);
}

static hal_status_t execute_text_data(const void *data, size_t len,
                                      hal_net_commands_source_t source,
                                      const hal_http_request_t *http_request,
                                      hal_websocket_client_t websocket_client,
                                      hal_net_command_response_t *response) {
  if (!data || !response) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    len = strlen((const char *)data);
  }
  if (len + 1u > HAL_NET_COMMANDS_TEXT_BUFFER_SIZE) {
    hal_net_command_response_reset(response);
    return finalize_response(response, HAL_EOVERFLOW, "request too large",
                             HAL_NET_COMMANDS_FORMAT_TEXT);
  }

  char buffer[HAL_NET_COMMANDS_TEXT_BUFFER_SIZE];
  memcpy(buffer, data, len);
  buffer[len] = '\0';
  trim_end(buffer);

  const char *start = skip_ws(buffer);
  if (*start == '\0') {
    return finalize_response(response, HAL_EINVAL, "empty command",
                             HAL_NET_COMMANDS_FORMAT_TEXT);
  }

  char *command = (char *)start;
  char *args = command;
  while (*args && *args != ' ' && *args != '\t' && *args != '\r' &&
         *args != '\n') {
    ++args;
  }
  if (*args) {
    *args++ = '\0';
    args = (char *)skip_ws(args);
  }

  net_command_source_context_t context = {};
  context.args_text = args ? args : "";
  context.http_request = http_request;
  context.websocket_client = websocket_client;

  hal_command_request_t request = {};
  request.source = source;
  request.encoding = HAL_COMMAND_ENCODING_TEXT;
  request.command = command;
  request.arguments = (const uint8_t *)context.args_text;
  request.arguments_length = strlen(context.args_text);
  request.source_context = &context;

  return dispatch_command(&request, response, HAL_NET_COMMANDS_FORMAT_TEXT);
}

static const cJSON *find_json_command_item(const cJSON *root) {
  const cJSON *command = cJSON_GetObjectItemCaseSensitive(root, "cmd");
  if (!cJSON_IsString(command)) {
    command = cJSON_GetObjectItemCaseSensitive(root, "command");
  }
  return command;
}

static const cJSON *find_json_args_item(const cJSON *root) {
  const cJSON *args = cJSON_GetObjectItemCaseSensitive(root, "args");
  if (!args) {
    args = cJSON_GetObjectItemCaseSensitive(root, "params");
  }
  return args;
}

static hal_status_t execute_json_data(const void *data, size_t len,
                                      hal_net_commands_source_t source,
                                      const hal_http_request_t *http_request,
                                      hal_websocket_client_t websocket_client,
                                      hal_net_command_response_t *response) {
  if (!data || !response) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    len = strlen((const char *)data);
  }
  if (len + 1u > HAL_NET_COMMANDS_TEXT_BUFFER_SIZE) {
    hal_net_command_response_reset(response);
    return finalize_response(response, HAL_EOVERFLOW, "request too large",
                             HAL_NET_COMMANDS_FORMAT_JSON);
  }

  char buffer[HAL_NET_COMMANDS_TEXT_BUFFER_SIZE];
  memcpy(buffer, data, len);
  buffer[len] = '\0';

  cJSON *root = cJSON_ParseWithOpts(buffer, NULL, 1);
  if (!root) {
    return finalize_response(response, HAL_EPROTO, "invalid json",
                             HAL_NET_COMMANDS_FORMAT_JSON);
  }

  hal_status_t result = HAL_OK;
  if (!cJSON_IsObject(root)) {
    result = finalize_response(response, HAL_EINVAL, "json root must be object",
                               HAL_NET_COMMANDS_FORMAT_JSON);
    cJSON_Delete(root);
    return result;
  }

  const cJSON *command = find_json_command_item(root);
  if (!cJSON_IsString(command) || !command->valuestring ||
      command->valuestring[0] == '\0') {
    result = finalize_response(response, HAL_EINVAL, "missing command",
                               HAL_NET_COMMANDS_FORMAT_JSON);
    cJSON_Delete(root);
    return result;
  }

  const cJSON *args = find_json_args_item(root);
  char *arguments_json = args != NULL ? cJSON_PrintUnformatted(args) : NULL;
  if (args != NULL && arguments_json == NULL) {
    result =
        finalize_response(response, HAL_ENOMEM, "json args allocation failed",
                          HAL_NET_COMMANDS_FORMAT_JSON);
    cJSON_Delete(root);
    return result;
  }

  net_command_source_context_t context = {};
  context.args_text =
      args != NULL && cJSON_IsString(args) && args->valuestring != NULL
          ? args->valuestring
          : "";
  context.json_root = root;
  context.json_args = args;
  context.http_request = http_request;
  context.websocket_client = websocket_client;

  hal_command_request_t request = {};
  request.source = source;
  request.encoding = HAL_COMMAND_ENCODING_JSON;
  request.command = command->valuestring;
  request.arguments = (const uint8_t *)arguments_json;
  request.arguments_length =
      arguments_json != NULL ? strlen(arguments_json) : 0u;
  request.source_context = &context;

  result = dispatch_command(&request, response, HAL_NET_COMMANDS_FORMAT_JSON);
  if (arguments_json != NULL) {
    cJSON_free(arguments_json);
  }
  cJSON_Delete(root);
  return result;
}

static hal_status_t execute_data(const void *data, size_t len,
                                 hal_net_commands_format_t format,
                                 hal_net_commands_source_t source,
                                 const hal_http_request_t *http_request,
                                 hal_websocket_client_t websocket_client,
                                 hal_net_command_response_t *response) {
  if (!response) {
    return HAL_EINVAL;
  }
  hal_net_command_response_reset(response);
  if (!data || !is_valid_format(format)) {
    return finalize_response(response, HAL_EINVAL, "invalid request",
                             HAL_NET_COMMANDS_FORMAT_TEXT);
  }

  if (format == HAL_NET_COMMANDS_FORMAT_AUTO) {
    const char *text = (const char *)data;
    size_t n = len;
    if (n == 0u) {
      n = strlen(text);
    }
    size_t i = 0u;
    while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' ||
                     text[i] == '\n')) {
      ++i;
    }
    format = (i < n && text[i] == '{') ? HAL_NET_COMMANDS_FORMAT_JSON
                                       : HAL_NET_COMMANDS_FORMAT_TEXT;
  }

  if (format == HAL_NET_COMMANDS_FORMAT_JSON) {
    return execute_json_data(data, len, source, http_request, websocket_client,
                             response);
  }
  return execute_text_data(data, len, source, http_request, websocket_client,
                           response);
}

static uint16_t status_to_http_code(hal_status_t status) {
  switch (status) {
  case HAL_OK:
    return 200u;
  case HAL_EINVAL:
  case HAL_EPROTO:
    return 400u;
  case HAL_EAUTH:
  case HAL_EPERM:
    return 403u;
  case HAL_ENOENT:
    return 404u;
  case HAL_EOVERFLOW:
    return 413u;
  default:
    return status < 0 ? 500u : 200u;
  }
}

static const char *http_reason(uint16_t code) {
  switch (code) {
  case 200u:
    return "OK";
  case 400u:
    return "Bad Request";
  case 403u:
    return "Forbidden";
  case 404u:
    return "Not Found";
  case 413u:
    return "Payload Too Large";
  default:
    return "Internal Server Error";
  }
}

static hal_status_t http_route_handler(const hal_http_request_t *request,
                                       hal_http_response_t *response,
                                       void *user) {
  const hal_net_commands_format_t *format =
      (const hal_net_commands_format_t *)user;
  if (format == NULL || !is_valid_format(*format)) {
    return HAL_EINVAL;
  }
  return hal_net_commands_handle_http_request(request, response, *format);
}

hal_status_t hal_net_commands_register(const char *name,
                                       hal_net_command_handler_t handler,
                                       void *user) {
  static_assert(sizeof(hal_net_command_handler_t) <=
                    JH_COMMAND_ROUTER_CALLBACK_STORAGE_SIZE,
                "network command callback storage is too small");
  if (handler == NULL) {
    return HAL_EINVAL;
  }

  hal_command_router_t router = NULL;
  hal_status_t status = hal_command_router_default(&router);
  if (status != HAL_OK) {
    return status;
  }

  jh_command_router_definition_t definition = {};
  definition.name = name;
  definition.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_DIRECT) |
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_HTTP) |
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_WEBSOCKET);
  definition.invoke = invoke_net_handler;
  definition.callback = reinterpret_cast<const void *>(&handler);
  definition.callback_size = sizeof(handler);
  definition.user = user;
  return jh_command_router_register_erased(router, &definition);
}

hal_status_t hal_net_commands_unregister(const char *name) {
  hal_command_router_t router = NULL;
  hal_status_t status = hal_command_router_default(&router);
  if (status != HAL_OK) {
    return status;
  }
  return hal_command_router_unregister(router, name);
}

void hal_net_commands_clear(void) {
  hal_command_router_t router = NULL;
  if (hal_command_router_default(&router) == HAL_OK) {
    (void)hal_command_router_clear(router);
  }
}

size_t hal_net_commands_count(void) {
  size_t count = 0u;
  hal_command_router_t router = NULL;
  if (hal_command_router_default(&router) == HAL_OK) {
    (void)hal_command_router_count(router, &count);
  }
  return count;
}

void hal_net_command_response_reset(hal_net_command_response_t *response) {
  hal_command_response_reset(response);
  if (response != NULL) {
    (void)hal_command_response_set_encoding(response,
                                            HAL_COMMAND_ENCODING_TEXT);
  }
}

hal_status_t
hal_net_command_response_set_status(hal_net_command_response_t *response,
                                    hal_status_t status, const char *message) {
  return hal_command_response_set_status(response, status, message);
}

hal_status_t
hal_net_command_response_set_content_type(hal_net_command_response_t *response,
                                          const char *content_type) {
  return hal_command_response_set_content_type(response, content_type);
}

hal_status_t
hal_net_command_response_write(hal_net_command_response_t *response,
                               const void *data, size_t len) {
  return hal_command_response_write(response, data, len);
}

hal_status_t
hal_net_command_response_write_str(hal_net_command_response_t *response,
                                   const char *text) {
  return hal_command_response_write_str(response, text);
}

hal_status_t
hal_net_command_response_write_json(hal_net_command_response_t *response,
                                    const cJSON *json) {
  if (!response || !json) {
    return HAL_EINVAL;
  }

  char *printed = cJSON_PrintUnformatted(json);
  if (!printed) {
    return HAL_ENOMEM;
  }

  hal_status_t status =
      hal_command_response_set_encoding(response, HAL_COMMAND_ENCODING_JSON);
  if (status == HAL_OK) {
    status = hal_net_command_response_write_str(response, printed);
  }
  cJSON_free(printed);
  return status;
}

hal_status_t
hal_net_commands_execute_text(const char *text,
                              hal_net_command_response_t *response) {
  return execute_data(text, 0u, HAL_NET_COMMANDS_FORMAT_TEXT,
                      HAL_NET_COMMANDS_SOURCE_DIRECT, NULL,
                      HAL_WEBSOCKET_INVALID_CLIENT, response);
}

hal_status_t
hal_net_commands_execute_json(const char *json, size_t len,
                              hal_net_command_response_t *response) {
  return execute_data(json, len, HAL_NET_COMMANDS_FORMAT_JSON,
                      HAL_NET_COMMANDS_SOURCE_DIRECT, NULL,
                      HAL_WEBSOCKET_INVALID_CLIENT, response);
}

hal_status_t hal_net_commands_execute(const void *data, size_t len,
                                      hal_net_commands_format_t format,
                                      hal_net_command_response_t *response) {
  return execute_data(data, len, format, HAL_NET_COMMANDS_SOURCE_DIRECT, NULL,
                      HAL_WEBSOCKET_INVALID_CLIENT, response);
}

hal_status_t
hal_net_commands_register_http_route(const char *path,
                                     hal_net_commands_format_t format) {
  if (!path || path[0] != '/' || !is_valid_format(format)) {
    return HAL_EINVAL;
  }
  static const hal_net_commands_format_t contexts[] = {
      HAL_NET_COMMANDS_FORMAT_TEXT,
      HAL_NET_COMMANDS_FORMAT_JSON,
      HAL_NET_COMMANDS_FORMAT_AUTO,
  };
  return hal_http_server_route(HAL_HTTP_METHOD_POST, path, http_route_handler,
                               (void *)&contexts[(int)format]);
}

hal_status_t
hal_net_commands_handle_http_request(const hal_http_request_t *request,
                                     hal_http_response_t *response,
                                     hal_net_commands_format_t format) {
  if (!request || !response || !is_valid_format(format)) {
    return HAL_EINVAL;
  }

  hal_net_command_response_t command_response;
  (void)execute_data(request->body, request->body_len, format,
                     HAL_NET_COMMANDS_SOURCE_HTTP, request,
                     HAL_WEBSOCKET_INVALID_CLIENT, &command_response);

  uint16_t code = status_to_http_code(command_response.status);
  hal_status_t out_status =
      hal_http_response_set_status(response, code, http_reason(code));
  if (out_status != HAL_OK) {
    return out_status;
  }
  out_status = hal_http_response_set_content_type(
      response, command_response.content_type);
  if (out_status != HAL_OK) {
    return out_status;
  }
  out_status = hal_http_response_write(response, command_response.body,
                                       command_response.body_len);
  return out_status == HAL_OK ? HAL_OK : out_status;
}

hal_status_t hal_net_commands_handle_websocket_message(
    hal_websocket_client_t client, hal_websocket_message_type_t type,
    const uint8_t *data, size_t len, hal_net_commands_format_t format) {
  if ((type != HAL_WEBSOCKET_MESSAGE_TEXT &&
       type != HAL_WEBSOCKET_MESSAGE_BINARY) ||
      !data || !is_valid_format(format)) {
    return HAL_EINVAL;
  }

  hal_net_command_response_t response;
  hal_status_t status =
      execute_data(data, len, format, HAL_NET_COMMANDS_SOURCE_WEBSOCKET, NULL,
                   client, &response);
  hal_status_t send_status = hal_websocket_send_text(client, response.body);
  return send_status == HAL_OK ? status : send_status;
}

const char *
hal_net_commands_format_to_string(hal_net_commands_format_t format) {
  switch (format) {
  case HAL_NET_COMMANDS_FORMAT_TEXT:
    return "TEXT";
  case HAL_NET_COMMANDS_FORMAT_JSON:
    return "JSON";
  case HAL_NET_COMMANDS_FORMAT_AUTO:
    return "AUTO";
  default:
    return "UNKNOWN";
  }
}

#endif /* HAL_ENABLE_NET_COMMANDS */

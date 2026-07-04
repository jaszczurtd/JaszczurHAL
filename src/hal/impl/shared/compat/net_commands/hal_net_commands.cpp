#include "hal/hal_net_commands.h"

#ifdef HAL_ENABLE_NET_COMMANDS

#include <stdio.h>
#include <string.h>

typedef struct {
  bool used;
  char name[HAL_NET_COMMANDS_NAME_MAX];
  hal_net_command_handler_t handler;
  void *user;
} net_command_slot_t;

static net_command_slot_t s_commands[HAL_NET_COMMANDS_MAX_COMMANDS];

static bool is_valid_format(hal_net_commands_format_t format) {
  return format == HAL_NET_COMMANDS_FORMAT_TEXT ||
         format == HAL_NET_COMMANDS_FORMAT_JSON ||
         format == HAL_NET_COMMANDS_FORMAT_AUTO;
}

static bool is_valid_name_char(char c) { return c > ' ' && c != '\x7f'; }

static bool is_valid_command_name(const char *name, size_t *out_len) {
  if (!name || name[0] == '\0') {
    return false;
  }
  size_t len = 0u;
  while (name[len]) {
    if (!is_valid_name_char(name[len])) {
      return false;
    }
    ++len;
    if (len >= HAL_NET_COMMANDS_NAME_MAX) {
      return false;
    }
  }
  if (out_len) {
    *out_len = len;
  }
  return true;
}

static net_command_slot_t *find_command(const char *name) {
  if (!name) {
    return NULL;
  }
  for (size_t i = 0u; i < HAL_NET_COMMANDS_MAX_COMMANDS; ++i) {
    if (s_commands[i].used && strcmp(s_commands[i].name, name) == 0) {
      return &s_commands[i];
    }
  }
  return NULL;
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

static hal_status_t dispatch_command(const hal_net_command_request_t *request,
                                     hal_net_command_response_t *response,
                                     hal_net_commands_format_t format) {
  net_command_slot_t *slot = find_command(request->command);
  if (!slot) {
    return finalize_response(response, HAL_ENOENT, "unknown command", format);
  }

  hal_status_t status = slot->handler(request, response, slot->user);
  if (status != HAL_OK && response->status == HAL_OK) {
    hal_net_command_response_set_status(response, status,
                                        hal_status_to_string(status));
  }
  return finalize_response(response, response->status, response->message,
                           format);
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

  hal_net_command_request_t request = {};
  request.source = source;
  request.command = command;
  request.args_text = args ? args : "";
  request.http_request = http_request;
  request.websocket_client = websocket_client;

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
  hal_net_command_request_t request = {};
  request.source = source;
  request.command = command->valuestring;
  request.args_text =
      cJSON_IsString(args) && args->valuestring ? args->valuestring : "";
  request.json_root = root;
  request.json_args = args;
  request.http_request = http_request;
  request.websocket_client = websocket_client;

  result = dispatch_command(&request, response, HAL_NET_COMMANDS_FORMAT_JSON);
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
  size_t name_len = 0u;
  if (!is_valid_command_name(name, &name_len) || !handler) {
    return HAL_EINVAL;
  }

  net_command_slot_t *slot = find_command(name);
  if (!slot) {
    for (size_t i = 0u; i < HAL_NET_COMMANDS_MAX_COMMANDS; ++i) {
      if (!s_commands[i].used) {
        slot = &s_commands[i];
        break;
      }
    }
  }
  if (!slot) {
    return HAL_ENOMEM;
  }

  memset(slot, 0, sizeof(*slot));
  slot->used = true;
  memcpy(slot->name, name, name_len);
  slot->name[name_len] = '\0';
  slot->handler = handler;
  slot->user = user;
  return HAL_OK;
}

hal_status_t hal_net_commands_unregister(const char *name) {
  size_t name_len = 0u;
  if (!is_valid_command_name(name, &name_len)) {
    return HAL_EINVAL;
  }
  (void)name_len;

  net_command_slot_t *slot = find_command(name);
  if (!slot) {
    return HAL_ENOENT;
  }
  memset(slot, 0, sizeof(*slot));
  return HAL_OK;
}

void hal_net_commands_clear(void) { memset(s_commands, 0, sizeof(s_commands)); }

size_t hal_net_commands_count(void) {
  size_t count = 0u;
  for (size_t i = 0u; i < HAL_NET_COMMANDS_MAX_COMMANDS; ++i) {
    if (s_commands[i].used) {
      ++count;
    }
  }
  return count;
}

void hal_net_command_response_reset(hal_net_command_response_t *response) {
  if (!response) {
    return;
  }
  response->status = HAL_OK;
  response->message = "OK";
  response->content_type = "text/plain";
  response->body_len = 0u;
  response->body[0] = '\0';
  response->overflow = false;
}

hal_status_t
hal_net_command_response_set_status(hal_net_command_response_t *response,
                                    hal_status_t status, const char *message) {
  if (!response || status == HAL_NONE) {
    return HAL_EINVAL;
  }
  response->status = status;
  response->message = message ? message : hal_status_to_string(status);
  return HAL_OK;
}

hal_status_t
hal_net_command_response_set_content_type(hal_net_command_response_t *response,
                                          const char *content_type) {
  if (!response || !content_type) {
    return HAL_EINVAL;
  }
  response->content_type = content_type;
  return HAL_OK;
}

hal_status_t
hal_net_command_response_write(hal_net_command_response_t *response,
                               const void *data, size_t len) {
  if (!response || (len > 0u && !data)) {
    return HAL_EINVAL;
  }
  if (response->body_len + len >= sizeof(response->body)) {
    response->overflow = true;
    return HAL_EOVERFLOW;
  }
  if (len > 0u) {
    memcpy(response->body + response->body_len, data, len);
    response->body_len += len;
    response->body[response->body_len] = '\0';
  }
  return HAL_OK;
}

hal_status_t
hal_net_command_response_write_str(hal_net_command_response_t *response,
                                   const char *text) {
  return text ? hal_net_command_response_write(response, text, strlen(text))
              : HAL_EINVAL;
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
      hal_net_command_response_set_content_type(response, "application/json");
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

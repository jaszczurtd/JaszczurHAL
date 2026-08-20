#include "hal/network/notify/hal_notify.h"

#ifdef HAL_ENABLE_NOTIFY_TELEGRAM

#include "hal/codecs/cjson/cJSON.h"
#include "hal/network/http/hal_http_client.h"
#include "hal/system/hal_system.h"
#include "utils/tools_api.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JH_TELEGRAM_API_HOST "api.telegram.org"
#define JH_TELEGRAM_PART_MARKER_RESERVE 32u

typedef struct {
  const char *bot_token;
  const char *default_chat_id;
  const char *api_host;
  uint16_t port;
  hal_notify_transport_t transport;
  const hal_tls_security_config_t *tls_security;
  bool disable_notification;
  bool disable_web_page_preview;
} jh_notify_telegram_state_t;

static bool non_empty(const char *value) {
  return value != NULL && value[0] != '\0';
}

static bool no_control_linebreaks(const char *value) {
  return value != NULL && strchr(value, '\r') == NULL &&
         strchr(value, '\n') == NULL;
}

static bool path_component_valid(const char *value) {
  return non_empty(value) && strpbrk(value, "\r\n /?#") == NULL;
}

static bool host_valid(const char *value) {
  return non_empty(value) && strpbrk(value, "\r\n/") == NULL;
}

static char ascii_lower(char value) {
  return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static bool is_public_telegram_host(const char *host) {
  size_t host_length = strlen(host);
  while (host_length > 0u && host[host_length - 1u] == '.') {
    --host_length;
  }
  const size_t expected_length = sizeof(JH_TELEGRAM_API_HOST) - 1u;
  if (host_length != expected_length) {
    return false;
  }
  for (size_t index = 0u; index < expected_length; ++index) {
    if (ascii_lower(host[index]) != JH_TELEGRAM_API_HOST[index]) {
      return false;
    }
  }
  return true;
}

static bool transport_valid(hal_notify_transport_t transport) {
  return transport == HAL_NOTIFY_TRANSPORT_HTTPS ||
         transport == HAL_NOTIFY_TRANSPORT_HTTP;
}

static const char *parse_mode_for_format(hal_notify_format_t format) {
  switch (format) {
  case HAL_NOTIFY_FORMAT_MARKDOWN:
    return "MarkdownV2";
  case HAL_NOTIFY_FORMAT_HTML:
    return "HTML";
  default:
    return NULL;
  }
}

static uint16_t default_port(hal_notify_transport_t transport) {
  return transport == HAL_NOTIFY_TRANSPORT_HTTP ? 80u : 443u;
}

static hal_status_t
telegram_configure_state(const hal_notify_telegram_config_t *config,
                         jh_notify_telegram_state_t *state) {
  if (config == NULL || state == NULL ||
      !path_component_valid(config->bot_token) ||
      !transport_valid(config->transport)) {
    return HAL_EINVAL;
  }

  const char *host =
      config->api_host != NULL ? config->api_host : JH_TELEGRAM_API_HOST;
  if (!host_valid(host)) {
    return HAL_EINVAL;
  }
  if (config->default_chat_id != NULL &&
      !no_control_linebreaks(config->default_chat_id)) {
    return HAL_EINVAL;
  }
  if (config->transport == HAL_NOTIFY_TRANSPORT_HTTP &&
      is_public_telegram_host(host)) {
    return HAL_ECONFIG;
  }
  if (config->transport == HAL_NOTIFY_TRANSPORT_HTTPS &&
      config->tls_security == NULL) {
    return HAL_ECONFIG;
  }

  memset(state, 0, sizeof(*state));
  state->bot_token = config->bot_token;
  state->default_chat_id = config->default_chat_id;
  state->api_host = host;
  state->port =
      config->port != 0u ? config->port : default_port(config->transport);
  state->transport = config->transport;
  state->tls_security = config->tls_security;
  state->disable_notification = config->disable_notification;
  state->disable_web_page_preview = config->disable_web_page_preview;
  return state->port != 0u ? HAL_OK : HAL_EINVAL;
}

static const char *severity_name(hal_notify_severity_t severity) {
  switch (severity) {
  case HAL_NOTIFY_SEVERITY_WARNING:
    return "WARNING";
  case HAL_NOTIFY_SEVERITY_ERROR:
    return "ERROR";
  case HAL_NOTIFY_SEVERITY_CRITICAL:
    return "CRITICAL";
  default:
    return "INFO";
  }
}

static bool append_bytes(char *buffer, size_t capacity, size_t *length,
                         const char *value, size_t value_length) {
  if (value_length >= capacity || *length > capacity - value_length - 1u) {
    return false;
  }
  if (value_length > 0u) {
    memcpy(buffer + *length, value, value_length);
    *length += value_length;
  }
  buffer[*length] = '\0';
  return true;
}

static bool append_string(char *buffer, size_t capacity, size_t *length,
                          const char *value) {
  return append_bytes(buffer, capacity, length, value, strlen(value));
}

static bool markdown_special(char value) {
  return strchr("_*[]()~`>#+-=|{}.!\\", value) != NULL;
}

static bool append_metadata(char *buffer, size_t capacity, size_t *length,
                            const char *value, hal_notify_format_t format) {
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    const char current = *cursor;
    if (format == HAL_NOTIFY_FORMAT_MARKDOWN && markdown_special(current)) {
      if (!append_string(buffer, capacity, length, "\\")) {
        return false;
      }
    } else if (format == HAL_NOTIFY_FORMAT_HTML) {
      const char *escaped = current == '&'   ? "&amp;"
                            : current == '<' ? "&lt;"
                            : current == '>' ? "&gt;"
                                             : NULL;
      if (escaped != NULL) {
        if (!append_string(buffer, capacity, length, escaped)) {
          return false;
        }
        continue;
      }
    }
    if (!append_bytes(buffer, capacity, length, &current, 1u)) {
      return false;
    }
  }
  return true;
}

static bool append_bracketed_metadata(char *buffer, size_t capacity,
                                      size_t *length, const char *value,
                                      hal_notify_format_t format) {
  const char *open = format == HAL_NOTIFY_FORMAT_MARKDOWN ? "\\[" : "[";
  const char *close = format == HAL_NOTIFY_FORMAT_MARKDOWN ? "\\]" : "]";
  return append_string(buffer, capacity, length, open) &&
         append_metadata(buffer, capacity, length, value, format) &&
         append_string(buffer, capacity, length, close);
}

static hal_status_t build_header(const hal_notify_message_t *message,
                                 char *buffer, size_t capacity,
                                 size_t *out_length) {
  size_t length = 0u;
  if (!append_bracketed_metadata(buffer, capacity, &length,
                                 severity_name(message->severity),
                                 message->format)) {
    return HAL_EOVERFLOW;
  }
  if (non_empty(message->device_name)) {
    if (!append_string(buffer, capacity, &length, " ") ||
        !append_bracketed_metadata(buffer, capacity, &length,
                                   message->device_name, message->format)) {
      return HAL_EOVERFLOW;
    }
  }
  if (non_empty(message->title) &&
      (!append_string(buffer, capacity, &length, " ") ||
       !append_string(buffer, capacity, &length, message->title))) {
    return HAL_EOVERFLOW;
  }
  *out_length = length;
  return HAL_OK;
}

static size_t choose_part_length(const char *body, size_t remaining,
                                 size_t capacity) {
  if (remaining <= capacity) {
    return remaining;
  }

  size_t end = capacity;
  while (end > 0u && (((unsigned char)body[end] & 0xC0u) == 0x80u)) {
    --end;
  }
  if (end == 0u) {
    return 0u;
  }

  const size_t preferred_floor = end / 2u;
  for (size_t index = end; index > preferred_floor; --index) {
    const char value = body[index - 1u];
    if (value == '\n' || value == ' ' || value == '\t') {
      return index;
    }
  }
  return end;
}

static hal_status_t count_parts(const char *body, size_t body_length,
                                size_t part_capacity, uint32_t *out_count) {
  size_t offset = 0u;
  uint32_t count = 0u;
  while (offset < body_length) {
    const size_t length =
        choose_part_length(body + offset, body_length - offset, part_capacity);
    if (length == 0u || count == UINT32_MAX) {
      return HAL_EOVERFLOW;
    }
    offset += length;
    ++count;
  }
  *out_count = count;
  return HAL_OK;
}

static hal_status_t build_part_text(const hal_notify_message_t *message,
                                    const char *body, size_t body_length,
                                    uint32_t part, uint32_t total, char *buffer,
                                    size_t capacity) {
  size_t length = 0u;
  hal_status_t status = build_header(message, buffer, capacity, &length);
  if (status != HAL_OK) {
    return status;
  }
  if (total > 1u) {
    char marker[32] = {};
    const int written =
        message->format == HAL_NOTIFY_FORMAT_MARKDOWN
            ? snprintf(marker, sizeof(marker), " \\(%lu/%lu\\)",
                       (unsigned long)part, (unsigned long)total)
            : snprintf(marker, sizeof(marker), " (%lu/%lu)",
                       (unsigned long)part, (unsigned long)total);
    if (written < 0 || (size_t)written >= sizeof(marker) ||
        !append_bytes(buffer, capacity, &length, marker, (size_t)written)) {
      return HAL_EOVERFLOW;
    }
  }
  if (!append_string(buffer, capacity, &length, "\n") ||
      !append_bytes(buffer, capacity, &length, body, body_length)) {
    return HAL_EOVERFLOW;
  }
  return HAL_OK;
}

static bool add_bool_if(cJSON *root, const char *name, bool value) {
  if (!value) {
    return true;
  }
  NONULL(cJSON_AddBoolToObject(root, name, true));
  return true;

error:
  return false;
}

static bool add_link_preview_options_if(cJSON *root, bool disabled) {
  cJSON *options = NULL;

  if (!disabled) {
    return true;
  }
  options = cJSON_CreateObject();
  NONULL(options);
  NONULL(cJSON_AddBoolToObject(options, "is_disabled", true));
  if (!cJSON_AddItemToObject(root, "link_preview_options", options)) {
    goto error;
  }
  return true;

error:
  if (options != NULL) {
    cJSON_Delete(options);
  }
  return false;
}

static hal_status_t build_json_body(const jh_notify_telegram_state_t *state,
                                    const hal_notify_message_t *message,
                                    const char *text, char **out_buffer) {
  *out_buffer = NULL;
  const char *chat_id = non_empty(message->destination)
                            ? message->destination
                            : state->default_chat_id;
  if (!non_empty(chat_id) || !no_control_linebreaks(chat_id)) {
    return HAL_EINVAL;
  }

  cJSON *root = NULL;
  hal_status_t status = HAL_ENOMEM;
  const char *parse_mode = parse_mode_for_format(message->format);
  const bool silent = state->disable_notification ||
                      ((message->flags & HAL_NOTIFY_MESSAGE_SILENT) != 0u);
  const bool suppress_preview =
      state->disable_web_page_preview ||
      ((message->flags & HAL_NOTIFY_MESSAGE_SUPPRESS_LINK_PREVIEW) != 0u);

  root = cJSON_CreateObject();
  NONULL(root);
  NONULL(cJSON_AddStringToObject(root, "chat_id", chat_id));
  NONULL(cJSON_AddStringToObject(root, "text", text));
  if (parse_mode != NULL) {
    NONULL(cJSON_AddStringToObject(root, "parse_mode", parse_mode));
  }
  if (!add_bool_if(root, "disable_notification", silent) ||
      !add_link_preview_options_if(root, suppress_preview)) {
    goto error;
  }
  *out_buffer = cJSON_PrintUnformatted(root);
  status = *out_buffer != NULL ? HAL_OK : HAL_ENOMEM;

error:
  if (root != NULL) {
    cJSON_Delete(root);
  }
  return status;
}

static hal_status_t map_api_error(int32_t code) {
  switch (code) {
  case 400:
    return HAL_EINVAL;
  case 401:
  case 403:
    return HAL_EAUTH;
  case 404:
    return HAL_ENOENT;
  case 409:
    return HAL_EBUSY;
  case 429:
    return HAL_EAGAIN;
  default:
    if (code >= 500 && code <= 599) {
      return HAL_EIO;
    }
    return HAL_EPROTO;
  }
}

static void parse_error_fields(const cJSON *root,
                               hal_notify_receipt_t *receipt) {
  if (receipt == NULL || root == NULL) {
    return;
  }
  const cJSON *error_code =
      cJSON_GetObjectItemCaseSensitive(root, "error_code");
  if (cJSON_IsNumber(error_code)) {
    receipt->provider_error = (int32_t)error_code->valuedouble;
  }
  const cJSON *parameters =
      cJSON_GetObjectItemCaseSensitive(root, "parameters");
  if (cJSON_IsObject(parameters)) {
    const cJSON *retry_after =
        cJSON_GetObjectItemCaseSensitive(parameters, "retry_after");
    if (cJSON_IsNumber(retry_after) && retry_after->valuedouble >= 0.0) {
      receipt->retry_after_s = (uint32_t)retry_after->valuedouble;
    }
  }
}

static void parse_message_id(const cJSON *root, hal_notify_receipt_t *receipt) {
  if (receipt == NULL || root == NULL) {
    return;
  }
  const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
  const cJSON *message_id =
      cJSON_GetObjectItemCaseSensitive(result, "message_id");
  if (cJSON_IsNumber(message_id)) {
    (void)snprintf(receipt->provider_message_id,
                   sizeof(receipt->provider_message_id), "%ld",
                   (long)message_id->valuedouble);
  }
}

static hal_status_t parse_telegram_response(char *body, size_t body_length,
                                            hal_notify_receipt_t *receipt) {
  cJSON *root = NULL;
  const cJSON *ok = NULL;
  const int32_t provider_status =
      receipt != NULL ? receipt->provider_status : 0;
  int32_t code = 0;
  hal_status_t status =
      provider_status >= 400 ? map_api_error(provider_status) : HAL_EPROTO;

  root = cJSON_ParseWithLength(body, body_length);
  NONULL(root);
  ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
  if (!cJSON_IsBool(ok)) {
    status = HAL_EPROTO;
    goto error;
  }

  if (!cJSON_IsTrue(ok)) {
    parse_error_fields(root, receipt);
    code = receipt != NULL && receipt->provider_error != 0
               ? receipt->provider_error
           : receipt != NULL ? receipt->provider_status
                             : 0;
    status = map_api_error(code);
    goto error;
  }

  parse_message_id(root, receipt);
  status = HAL_OK;

error:
  if (root != NULL) {
    cJSON_Delete(root);
  }
  return status;
}

static hal_status_t telegram_send_part(jh_notify_telegram_state_t *state,
                                       const hal_notify_message_t *message,
                                       const char *path, const char *text,
                                       uint32_t timeout_ms,
                                       hal_notify_receipt_t *receipt) {
  char *json = NULL;
  hal_status_t status = build_json_body(state, message, text, &json);
  if (status != HAL_OK) {
    return status;
  }

  const hal_http_client_header_t headers[] = {
      {"Content-Type", "application/json"},
      {"Accept", "application/json"},
  };
  hal_http_client_request_t request = {};
  status = hal_http_client_request_init(&request);
  if (status != HAL_OK) {
    cJSON_free(json);
    return status;
  }
  request.transport = state->transport == HAL_NOTIFY_TRANSPORT_HTTPS
                          ? HAL_HTTP_CLIENT_TRANSPORT_TLS
                          : HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT;
  request.host = state->api_host;
  request.port = state->port;
  request.method = "POST";
  request.path = path;
  request.headers = headers;
  request.header_count = sizeof(headers) / sizeof(headers[0]);
  request.body = json;
  request.body_length = strlen(json);
  request.timeout_ms = timeout_ms;
  request.tls_security = state->tls_security;

  char response_body[HAL_NOTIFY_TELEGRAM_RESPONSE_BUFFER_SIZE] = {};
  hal_http_client_response_t response = {};
  status = hal_http_client_perform_ex(&request, response_body,
                                      sizeof(response_body) - 1u, &response);
  cJSON_free(json);
  if (receipt != NULL) {
    receipt->provider_status = response.status_code;
  }
  if (status == HAL_EOVERFLOW) {
    if (receipt != NULL) {
      receipt->flags |= HAL_NOTIFY_RECEIPT_RESPONSE_TRUNCATED;
    }
    return status;
  }
  if (status != HAL_OK) {
    hal_derr("Telegram HTTP transport failed: %s HTTP=%u bytes=%zu "
             "declared=%zu known=%d",
             hal_status_to_string(status), (unsigned)response.status_code,
             response.body_length, response.content_length,
             response.content_length_known ? 1 : 0);
    return status;
  }

  if (response.status_code < 200u || response.status_code >= 300u) {
    (void)parse_telegram_response(response_body, response.body_length, receipt);
    const int32_t code = receipt != NULL && receipt->provider_error != 0
                             ? receipt->provider_error
                             : (int32_t)response.status_code;
    return map_api_error(code);
  }
  status =
      parse_telegram_response(response_body, response.body_length, receipt);
  if (status != HAL_OK) {
    hal_derr("Telegram response parse failed: %s HTTP=%u bytes=%zu",
             hal_status_to_string(status), (unsigned)response.status_code,
             response.body_length);
  }
  return status;
}

static hal_status_t telegram_open(void *state, const void *config) {
  return telegram_configure_state(
      static_cast<const hal_notify_telegram_config_t *>(config),
      static_cast<jh_notify_telegram_state_t *>(state));
}

static hal_status_t telegram_send(void *state_ptr,
                                  const hal_notify_message_t *message,
                                  hal_notify_receipt_t *receipt) {
  jh_notify_telegram_state_t *state =
      static_cast<jh_notify_telegram_state_t *>(state_ptr);
  char path[HAL_NOTIFY_TELEGRAM_PATH_MAX] = {};
  const int path_length =
      snprintf(path, sizeof(path), "/bot%s/sendMessage", state->bot_token);
  if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
    return HAL_EOVERFLOW;
  }

  char *text = (char *)malloc(HAL_NOTIFY_TELEGRAM_TEXT_MAX + 1u);
  if (text == NULL) {
    return HAL_ENOMEM;
  }

  size_t header_length = 0u;
  hal_status_t status = build_header(
      message, text, HAL_NOTIFY_TELEGRAM_TEXT_MAX + 1u, &header_length);
  const size_t body_length = strlen(message->body);
  const bool needs_split =
      status == HAL_OK &&
      (header_length + 1u > HAL_NOTIFY_TELEGRAM_TEXT_MAX ||
       body_length > HAL_NOTIFY_TELEGRAM_TEXT_MAX - header_length - 1u);
  if (status == HAL_OK && needs_split &&
      message->format != HAL_NOTIFY_FORMAT_TEXT) {
    status = HAL_EOVERFLOW;
  }

  size_t part_capacity = body_length;
  uint32_t parts_total = 1u;
  if (status == HAL_OK && needs_split) {
    if (header_length + 1u + JH_TELEGRAM_PART_MARKER_RESERVE >=
        HAL_NOTIFY_TELEGRAM_TEXT_MAX) {
      status = HAL_EOVERFLOW;
    } else {
      part_capacity = HAL_NOTIFY_TELEGRAM_TEXT_MAX - header_length - 1u -
                      JH_TELEGRAM_PART_MARKER_RESERVE;
      status =
          count_parts(message->body, body_length, part_capacity, &parts_total);
    }
  }
  if (receipt != NULL && status == HAL_OK) {
    receipt->parts_total = parts_total;
  }

  const uint32_t started_ms = hal_millis();
  size_t offset = 0u;
  uint32_t parts_sent = 0u;
  while (status == HAL_OK && offset < body_length) {
    const size_t length =
        needs_split ? choose_part_length(message->body + offset,
                                         body_length - offset, part_capacity)
                    : body_length;
    if (length == 0u) {
      status = HAL_EOVERFLOW;
      break;
    }
    status = build_part_text(message, message->body + offset, length,
                             parts_sent + 1u, parts_total, text,
                             HAL_NOTIFY_TELEGRAM_TEXT_MAX + 1u);
    if (status != HAL_OK) {
      break;
    }

    const uint32_t elapsed_ms = (uint32_t)(hal_millis() - started_ms);
    if (elapsed_ms >= message->timeout_ms) {
      status = HAL_ETIMEOUT;
      break;
    }
    status = telegram_send_part(state, message, path, text,
                                message->timeout_ms - elapsed_ms, receipt);
    if (status != HAL_OK) {
      break;
    }
    offset += length;
    ++parts_sent;
    if (receipt != NULL) {
      receipt->parts_sent = parts_sent;
    }
  }

  if (receipt != NULL && parts_sent > 0u && parts_sent < parts_total) {
    receipt->flags |= HAL_NOTIFY_RECEIPT_PARTIAL_DELIVERY;
  }
  free(text);
  return status;
}

static hal_status_t telegram_poll(void *) { return HAL_OK; }

static hal_status_t telegram_close(void *state) {
  if (state != NULL) {
    memset(state, 0, sizeof(jh_notify_telegram_state_t));
  }
  return HAL_OK;
}

hal_status_t
hal_notify_telegram_config_init(hal_notify_telegram_config_t *config) {
  if (config == NULL) {
    return HAL_EINVAL;
  }
  memset(config, 0, sizeof(*config));
  config->transport = HAL_NOTIFY_TRANSPORT_HTTPS;
  return HAL_OK;
}

const hal_notify_backend_t *hal_notify_telegram_backend(void) {
  static const hal_notify_backend_t backend = {
      HAL_NOTIFY_BACKEND_API_VERSION,
      "telegram",
      sizeof(jh_notify_telegram_state_t),
      telegram_open,
      telegram_send,
      telegram_poll,
      telegram_close,
  };
  return &backend;
}

#endif /* HAL_ENABLE_NOTIFY_TELEGRAM */

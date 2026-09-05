/** @file Target-neutral HTTP server service over HAL TCP. */
#include "hal/network/http/hal_http_server.h"
#include "hal/network/http/jh_http_text.h"

#ifdef HAL_ENABLE_HTTP_SERVER

#include "hal/network/hal_tcp.h"
#include "hal/system/hal_system.h"

#include <string.h>

typedef struct {
  char name[HAL_HTTP_SERVER_RESPONSE_HEADER_NAME_MAX];
  char value[HAL_HTTP_SERVER_RESPONSE_HEADER_VALUE_MAX];
} hal_http_response_header_storage_t;

struct hal_http_response_t {
  uint16_t status_code;
  const char *reason;
  const char *content_type;
  hal_http_response_header_storage_t
      headers[HAL_HTTP_SERVER_MAX_RESPONSE_HEADERS];
  size_t header_count;
  char body[HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE];
  size_t body_len;
  bool overflow;
};

typedef struct {
  hal_http_method_t method;
  char path[HAL_HTTP_SERVER_ROUTE_PATH_MAX];
  bool prefix;
  hal_http_handler_t handler;
  void *user;
} hal_http_route_t;

typedef struct {
  hal_tcp_socket_t socket;
  hal_net_endpoint_t remote;
  char request[HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE];
  size_t request_len;
  char response[HAL_HTTP_SERVER_RESPONSE_HEADER_SIZE +
                HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE];
  size_t response_len;
  size_t response_sent;
  uint32_t response_started_ms;
  uint32_t accepted_ms;
  uint32_t last_activity_ms;
} hal_http_client_t;

typedef enum {
  HTTP_REQUEST_PENDING = 0,
  HTTP_REQUEST_COMPLETE,
  HTTP_REQUEST_BAD,
  HTTP_REQUEST_TOO_LARGE
} http_request_state_t;

static hal_tcp_listener_t s_listener;
static hal_http_route_t s_routes[HAL_HTTP_SERVER_MAX_ROUTES];
static hal_http_client_t s_clients[HAL_HTTP_SERVER_MAX_CLIENTS];

static void reset_response(hal_http_response_t *response) {
  response->status_code = 200u;
  response->reason = "OK";
  response->content_type = "text/plain";
  response->header_count = 0u;
  response->body_len = 0u;
  response->body[0] = '\0';
  response->overflow = false;
}

static void clear_client(hal_http_client_t *client) {
  if (client->socket) {
    hal_tcp_socket_close(client->socket);
  }
  memset(client, 0, sizeof(*client));
  client->remote.family = HAL_NET_AF_UNSPEC;
}

#define append_header jh_http_append_text
#define lower_ascii jh_http_lower_ascii
#define str_ieq jh_http_str_ieq
#define str_nieq jh_http_str_nieq

static bool valid_header_name(const char *name) {
  if (!name || name[0] == '\0') {
    return false;
  }
  for (const char *p = name; *p; ++p) {
    const char c = *p;
    if (c <= ' ' || c == ':' || c == '\x7f') {
      return false;
    }
  }
  return true;
}

static bool append_uint(char *out, size_t out_size, size_t *pos,
                        uint32_t value) {
  char tmp[11];
  size_t len = 0u;
  do {
    tmp[len++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value && len < sizeof(tmp));

  if (*pos + len >= out_size) {
    return false;
  }
  while (len) {
    out[(*pos)++] = tmp[--len];
  }
  out[*pos] = '\0';
  return true;
}

static bool prepare_response(hal_http_client_t *client,
                             const hal_http_response_t *response,
                             bool suppress_body) {
  char *header = client->response;
  const size_t header_size = HAL_HTTP_SERVER_RESPONSE_HEADER_SIZE;
  size_t pos = 0u;

  if (!append_header(header, header_size, &pos, "HTTP/1.1 ") ||
      !append_uint(header, header_size, &pos, response->status_code) ||
      !append_header(header, header_size, &pos, " ") ||
      !append_header(header, header_size, &pos, response->reason) ||
      !append_header(header, header_size, &pos, "\r\nContent-Type: ") ||
      !append_header(header, header_size, &pos, response->content_type) ||
      !append_header(header, header_size, &pos, "\r\nContent-Length: ") ||
      !append_uint(header, header_size, &pos, (uint32_t)response->body_len)) {
    return false;
  }

  for (size_t i = 0u; i < response->header_count; ++i) {
    if (!append_header(header, header_size, &pos, "\r\n") ||
        !append_header(header, header_size, &pos, response->headers[i].name) ||
        !append_header(header, header_size, &pos, ": ") ||
        !append_header(header, header_size, &pos, response->headers[i].value)) {
      return false;
    }
  }

  if (!append_header(header, header_size, &pos,
                     "\r\nConnection: close\r\n\r\n")) {
    return false;
  }

  const size_t body_len = suppress_body ? 0u : response->body_len;
  if (body_len > 0u) {
    memcpy(client->response + pos, response->body, body_len);
  }
  client->response_len = pos + body_len;
  client->response_sent = 0u;
  client->response_started_ms = hal_millis();
  client->last_activity_ms = client->response_started_ms;
  return true;
}

static void poll_response(hal_http_client_t *client) {
  const uint32_t now_ms = hal_millis();
  if ((HAL_HTTP_SERVER_RESPONSE_TIMEOUT_MS > 0u &&
       hal_elapsed_u32(now_ms, client->response_started_ms,
                       HAL_HTTP_SERVER_RESPONSE_TIMEOUT_MS)) ||
      (HAL_HTTP_SERVER_IDLE_TIMEOUT_MS > 0u &&
       hal_elapsed_u32(now_ms, client->last_activity_ms,
                       HAL_HTTP_SERVER_IDLE_TIMEOUT_MS))) {
    clear_client(client);
    return;
  }

  const size_t remaining = client->response_len - client->response_sent;
  size_t sent = 0u;
  const hal_status_t status = hal_tcp_socket_send_ex(
      client->socket, client->response + client->response_sent, remaining,
      &sent);
  if (sent > remaining || (status != HAL_OK && status != HAL_EAGAIN)) {
    clear_client(client);
    return;
  }
  client->response_sent += sent;
  if (sent > 0u) {
    client->last_activity_ms = hal_millis();
  }
  if (client->response_sent == client->response_len) {
    clear_client(client);
  }
}

static void start_response(hal_http_client_t *client,
                           const hal_http_response_t *response,
                           bool suppress_body) {
  if (!prepare_response(client, response, suppress_body)) {
    clear_client(client);
    return;
  }
  poll_response(client);
}

static void start_text_error_response(hal_http_client_t *client,
                                      uint16_t status_code, const char *reason,
                                      const char *body) {
  hal_http_response_t response;
  reset_response(&response);
  hal_http_response_set_status(&response, status_code, reason);
  hal_http_response_write_str(&response, body);
  start_response(client, &response, false);
}

static char *find_crlf(char *s) {
  while (s && *s) {
    if (s[0] == '\r' && s[1] == '\n') {
      return s;
    }
    ++s;
  }
  return NULL;
}

static hal_http_method_t parse_method(char *method) {
  if (strcmp(method, "GET") == 0) {
    return HAL_HTTP_METHOD_GET;
  }
  if (strcmp(method, "HEAD") == 0) {
    return HAL_HTTP_METHOD_HEAD;
  }
  if (strcmp(method, "POST") == 0) {
    return HAL_HTTP_METHOD_POST;
  }
  if (strcmp(method, "PUT") == 0) {
    return HAL_HTTP_METHOD_PUT;
  }
  if (strcmp(method, "DELETE") == 0) {
    return HAL_HTTP_METHOD_DELETE;
  }
  if (strcmp(method, "OPTIONS") == 0) {
    return HAL_HTTP_METHOD_OPTIONS;
  }
  return HAL_HTTP_METHOD_UNKNOWN;
}

static const hal_http_route_t *find_route(hal_http_method_t method,
                                          const char *path) {
  const hal_http_route_t *prefix_route = NULL;
  size_t prefix_len = 0u;

  for (size_t i = 0u; i < HAL_HTTP_SERVER_MAX_ROUTES; ++i) {
    if (!s_routes[i].handler || s_routes[i].method != method) {
      continue;
    }

    if (!s_routes[i].prefix && strcmp(s_routes[i].path, path) == 0) {
      return &s_routes[i];
    }

    if (s_routes[i].prefix) {
      size_t len = strlen(s_routes[i].path);
      if (len > prefix_len && strncmp(s_routes[i].path, path, len) == 0 &&
          (s_routes[i].path[len - 1u] == '/' || path[len] == '\0' ||
           path[len] == '/')) {
        prefix_route = &s_routes[i];
        prefix_len = len;
      }
    }
  }
  return prefix_route;
}

static bool header_name_equals(const char *line, size_t name_len,
                               const char *expected) {
  return strlen(expected) == name_len && str_nieq(line, expected, name_len);
}

static http_request_state_t parse_message_length(const char *request,
                                                 const char *headers_end,
                                                 size_t body_capacity,
                                                 size_t *content_len_out) {
  const char *line = strstr(request, "\r\n");
  bool content_length_seen = false;
  size_t content_len = 0u;
  if (!line || line > headers_end) {
    return HTTP_REQUEST_BAD;
  }
  if (line == headers_end) {
    *content_len_out = 0u;
    return HTTP_REQUEST_COMPLETE;
  }
  line += 2;

  while (line < headers_end) {
    const char *line_end = strstr(line, "\r\n");
    if (!line_end || line_end > headers_end) {
      return HTTP_REQUEST_BAD;
    }
    const char *colon =
        (const char *)memchr(line, ':', (size_t)(line_end - line));
    if (!colon) {
      return HTTP_REQUEST_BAD;
    }

    size_t name_len = (size_t)(colon - line);
    while (name_len > 0u &&
           (line[name_len - 1u] == ' ' || line[name_len - 1u] == '\t')) {
      --name_len;
    }
    const char *value = colon + 1;
    while (value < line_end && (*value == ' ' || *value == '\t')) {
      ++value;
    }
    const char *value_end = line_end;
    while (value_end > value &&
           (value_end[-1] == ' ' || value_end[-1] == '\t')) {
      --value_end;
    }

    if (header_name_equals(line, name_len, "Transfer-Encoding")) {
      return HTTP_REQUEST_BAD;
    }
    if (header_name_equals(line, name_len, "Content-Length")) {
      if (content_length_seen || value == value_end) {
        return HTTP_REQUEST_BAD;
      }
      content_length_seen = true;
      size_t parsed = 0u;
      for (const char *p = value; p < value_end; ++p) {
        if (*p < '0' || *p > '9') {
          return HTTP_REQUEST_BAD;
        }
        const size_t digit = (size_t)(*p - '0');
        if (parsed > (SIZE_MAX - digit) / 10u) {
          return HTTP_REQUEST_TOO_LARGE;
        }
        parsed = parsed * 10u + digit;
      }
      content_len = parsed;
    }
    line = line_end + 2;
  }

  if (content_len > body_capacity) {
    return HTTP_REQUEST_TOO_LARGE;
  }
  *content_len_out = content_len;
  return HTTP_REQUEST_COMPLETE;
}

static char *trim_left(char *text) {
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  return text;
}

static void trim_right(char *text) {
  size_t len = strlen(text);
  while (len > 0u && (text[len - 1u] == ' ' || text[len - 1u] == '\t')) {
    text[--len] = '\0';
  }
}

static size_t parse_request_headers(char *headers_start, char *body_start,
                                    hal_http_header_t *headers,
                                    size_t max_headers) {
  size_t count = 0u;
  char *line = headers_start;
  while (line && line < body_start) {
    char *next = find_crlf(line);
    if (!next) {
      break;
    }
    *next = '\0';
    if (line[0] == '\0') {
      break;
    }

    char *colon = strchr(line, ':');
    if (colon && count < max_headers) {
      *colon = '\0';
      char *name = line;
      char *value = trim_left(colon + 1);
      trim_right(name);
      trim_right(value);
      if (valid_header_name(name)) {
        headers[count].name = name;
        headers[count].value = value;
        ++count;
      }
    }
    line = next + 2;
  }
  return count;
}

static http_request_state_t request_state(const hal_http_client_t *client,
                                          size_t *content_len_out) {
  const char *end = strstr(client->request, "\r\n\r\n");
  if (!end) {
    return HTTP_REQUEST_PENDING;
  }
  const size_t header_len = (size_t)(end - client->request) + 4u;
  if (header_len >= sizeof(client->request)) {
    return HTTP_REQUEST_TOO_LARGE;
  }
  size_t content_len = 0u;
  http_request_state_t state = parse_message_length(
      client->request, end, sizeof(client->request) - header_len - 1u,
      &content_len);
  if (state != HTTP_REQUEST_COMPLETE) {
    return state;
  }
  *content_len_out = content_len;
  return client->request_len >= header_len + content_len ? HTTP_REQUEST_COMPLETE
                                                         : HTTP_REQUEST_PENDING;
}

static void process_request(hal_http_client_t *client, size_t body_len) {
  hal_http_response_t response;
  reset_response(&response);

  char *request_line_end = find_crlf(client->request);
  if (!request_line_end) {
    start_text_error_response(client, 400u, "Bad Request", "Bad Request\n");
    return;
  }

  *request_line_end = '\0';
  char *method_s = client->request;
  char *target = strchr(method_s, ' ');
  if (!target) {
    start_text_error_response(client, 400u, "Bad Request", "Bad Request\n");
    return;
  }
  *target++ = '\0';
  char *version = strchr(target, ' ');
  if (!version) {
    start_text_error_response(client, 400u, "Bad Request", "Bad Request\n");
    return;
  }
  *version++ = '\0';

  hal_http_method_t method = parse_method(method_s);
  char *query = strchr(target, '?');
  if (query) {
    *query++ = '\0';
  }

  char *body = strstr(request_line_end + 2, "\r\n\r\n");
  hal_http_header_t headers[HAL_HTTP_SERVER_MAX_REQUEST_HEADERS];
  size_t header_count = 0u;
  if (body) {
    char *headers_start = request_line_end + 2;
    body += 4;
    header_count = parse_request_headers(headers_start, body, headers,
                                         HAL_HTTP_SERVER_MAX_REQUEST_HEADERS);
    body[body_len] = '\0';
  }

  if (method == HAL_HTTP_METHOD_UNKNOWN || strncmp(version, "HTTP/", 5) != 0 ||
      target[0] != '/') {
    start_text_error_response(client, 400u, "Bad Request", "Bad Request\n");
    return;
  }

  hal_http_request_t request = {};
  request.method = method;
  request.path = target;
  request.query = query ? query : "";
  request.body = body ? body : "";
  request.body_len = body_len;
  request.headers = headers;
  request.header_count = header_count;
  request.remote = client->remote;

  const hal_http_route_t *route = find_route(method, request.path);
  if (!route) {
    hal_http_response_set_status(&response, 404u, "Not Found");
    hal_http_response_write_str(&response, "Not Found\n");
  } else if (route->handler(&request, &response, route->user) != HAL_OK) {
    hal_http_response_set_status(&response, 500u, "Internal Server Error");
    response.body_len = 0u;
    response.body[0] = '\0';
    hal_http_response_write_str(&response, "Internal Server Error\n");
  }

  start_response(client, &response, method == HAL_HTTP_METHOD_HEAD);
}

static hal_status_t route_common(hal_http_method_t method, const char *path,
                                 bool prefix, hal_http_handler_t handler,
                                 void *user) {
  if (method == HAL_HTTP_METHOD_UNKNOWN || !path || path[0] != '/' ||
      !handler) {
    return HAL_EINVAL;
  }
  const size_t path_len = strlen(path);
  if (path_len >= HAL_HTTP_SERVER_ROUTE_PATH_MAX) {
    return HAL_EOVERFLOW;
  }

  for (size_t i = 0u; i < HAL_HTTP_SERVER_MAX_ROUTES; ++i) {
    if (s_routes[i].handler && s_routes[i].method == method &&
        s_routes[i].prefix == prefix && strcmp(s_routes[i].path, path) == 0) {
      s_routes[i].handler = handler;
      s_routes[i].user = user;
      return HAL_OK;
    }
  }

  for (size_t i = 0u; i < HAL_HTTP_SERVER_MAX_ROUTES; ++i) {
    if (!s_routes[i].handler) {
      s_routes[i].method = method;
      memcpy(s_routes[i].path, path, path_len + 1u);
      s_routes[i].prefix = prefix;
      s_routes[i].handler = handler;
      s_routes[i].user = user;
      return HAL_OK;
    }
  }

  return HAL_ENOMEM;
}

hal_status_t hal_http_server_route(hal_http_method_t method, const char *path,
                                   hal_http_handler_t handler, void *user) {
  return route_common(method, path, false, handler, user);
}

hal_status_t hal_http_server_route_prefix(hal_http_method_t method,
                                          const char *path_prefix,
                                          hal_http_handler_t handler,
                                          void *user) {
  return route_common(method, path_prefix, true, handler, user);
}

void hal_http_server_clear_routes(void) {
  memset(s_routes, 0, sizeof(s_routes));
}

hal_status_t hal_http_server_start(uint16_t port) {
  if (s_listener) {
    return HAL_OK;
  }
  if (port == 0u) {
    return HAL_EINVAL;
  }

  s_listener = hal_tcp_listener_open();
  if (!s_listener) {
    return HAL_ENOMEM;
  }

  hal_net_endpoint_t local = {};
  local.family = HAL_NET_AF_INET;
  local.addr_len = HAL_NET_IPV4_ADDR_LEN;
  local.addr[0] = 0u;
  local.addr[1] = 0u;
  local.addr[2] = 0u;
  local.addr[3] = 0u;
  local.port = port;

  if (!hal_tcp_listener_bind(s_listener, &local) ||
      !hal_tcp_listener_listen(s_listener, HAL_HTTP_SERVER_DEFAULT_BACKLOG)) {
    hal_tcp_listener_close(s_listener);
    s_listener = NULL;
    return HAL_EIO;
  }

  return HAL_OK;
}

void hal_http_server_stop(void) {
  for (size_t i = 0u; i < HAL_HTTP_SERVER_MAX_CLIENTS; ++i) {
    clear_client(&s_clients[i]);
  }
  if (s_listener) {
    hal_tcp_listener_close(s_listener);
    s_listener = NULL;
  }
}

bool hal_http_server_is_running(void) { return s_listener != NULL; }

void hal_http_server_poll(void) {
  if (!s_listener) {
    return;
  }

  while (hal_tcp_listener_can_accept(s_listener)) {
    hal_http_client_t *slot = NULL;
    for (size_t i = 0u; i < HAL_HTTP_SERVER_MAX_CLIENTS; ++i) {
      if (!s_clients[i].socket) {
        slot = &s_clients[i];
        break;
      }
    }

    hal_net_endpoint_t remote = {};
    hal_tcp_socket_t socket = hal_tcp_listener_accept(s_listener, &remote, 0u);
    if (!socket) {
      break;
    }
    if (!slot) {
      hal_tcp_socket_close(socket);
      break;
    }

    memset(slot, 0, sizeof(*slot));
    slot->socket = socket;
    slot->remote = remote;
    slot->accepted_ms = hal_millis();
    slot->last_activity_ms = slot->accepted_ms;
  }

  for (size_t i = 0u; i < HAL_HTTP_SERVER_MAX_CLIENTS; ++i) {
    hal_http_client_t *client = &s_clients[i];
    if (!client->socket) {
      continue;
    }

    if (!hal_tcp_socket_is_connected(client->socket)) {
      clear_client(client);
      continue;
    }

    if (client->response_len > 0u) {
      poll_response(client);
      continue;
    }

    if (hal_tcp_socket_can_recv(client->socket) &&
        client->request_len + 1u < sizeof(client->request)) {
      int rc = hal_tcp_socket_recv(
          client->socket, client->request + client->request_len,
          sizeof(client->request) - client->request_len - 1u, 0u);
      if (rc < 0) {
        clear_client(client);
        continue;
      }
      client->request_len += (size_t)rc;
      client->request[client->request_len] = '\0';
      if (rc > 0) {
        client->last_activity_ms = hal_millis();
      }
    }

    if (client->request_len + 1u >= sizeof(client->request)) {
      start_text_error_response(client, 413u, "Payload Too Large",
                                "Payload Too Large\n");
      continue;
    }

    size_t content_len = 0u;
    http_request_state_t state = request_state(client, &content_len);
    if (state == HTTP_REQUEST_BAD) {
      start_text_error_response(client, 400u, "Bad Request", "Bad Request\n");
      continue;
    }
    if (state == HTTP_REQUEST_TOO_LARGE) {
      start_text_error_response(client, 413u, "Payload Too Large",
                                "Payload Too Large\n");
      continue;
    }
    if (state == HTTP_REQUEST_COMPLETE) {
      process_request(client, content_len);
      continue;
    }

    const uint32_t now_ms = hal_millis();
    const bool first_byte_timeout =
        client->request_len == 0u &&
        HAL_HTTP_SERVER_FIRST_BYTE_TIMEOUT_MS > 0u &&
        hal_elapsed_u32(now_ms, client->accepted_ms,
                        HAL_HTTP_SERVER_FIRST_BYTE_TIMEOUT_MS);
    const bool request_timeout =
        HAL_HTTP_SERVER_REQUEST_TIMEOUT_MS > 0u &&
        hal_elapsed_u32(now_ms, client->accepted_ms,
                        HAL_HTTP_SERVER_REQUEST_TIMEOUT_MS);
    const bool idle_timeout = client->request_len > 0u &&
                              HAL_HTTP_SERVER_IDLE_TIMEOUT_MS > 0u &&
                              hal_elapsed_u32(now_ms, client->last_activity_ms,
                                              HAL_HTTP_SERVER_IDLE_TIMEOUT_MS);
    if (first_byte_timeout || request_timeout || idle_timeout) {
      start_text_error_response(client, 408u, "Request Timeout",
                                "Request Timeout\n");
    }
  }
}

hal_status_t hal_http_response_set_status(hal_http_response_t *response,
                                          uint16_t status_code,
                                          const char *reason) {
  if (!response || status_code < 100u || status_code > 599u || !reason) {
    return HAL_EINVAL;
  }
  response->status_code = status_code;
  response->reason = reason;
  return HAL_OK;
}

hal_status_t hal_http_response_set_content_type(hal_http_response_t *response,
                                                const char *content_type) {
  if (!response || !content_type) {
    return HAL_EINVAL;
  }
  response->content_type = content_type;
  return HAL_OK;
}

hal_status_t hal_http_response_set_header(hal_http_response_t *response,
                                          const char *name, const char *value) {
  if (!response || !valid_header_name(name) || !value) {
    return HAL_EINVAL;
  }
  if (strlen(name) >= HAL_HTTP_SERVER_RESPONSE_HEADER_NAME_MAX ||
      strlen(value) >= HAL_HTTP_SERVER_RESPONSE_HEADER_VALUE_MAX) {
    return HAL_EOVERFLOW;
  }

  hal_http_response_header_storage_t *slot = NULL;
  for (size_t i = 0u; i < response->header_count; ++i) {
    if (str_ieq(response->headers[i].name, name)) {
      slot = &response->headers[i];
      break;
    }
  }

  if (!slot) {
    if (response->header_count >= HAL_HTTP_SERVER_MAX_RESPONSE_HEADERS) {
      return HAL_ENOMEM;
    }
    slot = &response->headers[response->header_count++];
  }

  size_t name_len = strlen(name);
  size_t value_len = strlen(value);
  memcpy(slot->name, name, name_len + 1u);
  memcpy(slot->value, value, value_len + 1u);
  return HAL_OK;
}

hal_status_t hal_http_response_write(hal_http_response_t *response,
                                     const void *data, size_t len) {
  return jh_buffered_response_write(response, data, len);
}

hal_status_t hal_http_response_write_str(hal_http_response_t *response,
                                         const char *text) {
  return text ? hal_http_response_write(response, text, strlen(text))
              : HAL_EINVAL;
}

const char *hal_http_request_get_header(const hal_http_request_t *request,
                                        const char *name) {
  if (!request || !name) {
    return NULL;
  }
  for (size_t i = 0u; i < request->header_count; ++i) {
    if (str_ieq(request->headers[i].name, name)) {
      return request->headers[i].value;
    }
  }
  return NULL;
}

const char *hal_http_method_to_string(hal_http_method_t method) {
  switch (method) {
  case HAL_HTTP_METHOD_GET:
    return "GET";
  case HAL_HTTP_METHOD_HEAD:
    return "HEAD";
  case HAL_HTTP_METHOD_POST:
    return "POST";
  case HAL_HTTP_METHOD_PUT:
    return "PUT";
  case HAL_HTTP_METHOD_DELETE:
    return "DELETE";
  case HAL_HTTP_METHOD_OPTIONS:
    return "OPTIONS";
  default:
    return "UNKNOWN";
  }
}

#endif /* HAL_ENABLE_HTTP_SERVER */

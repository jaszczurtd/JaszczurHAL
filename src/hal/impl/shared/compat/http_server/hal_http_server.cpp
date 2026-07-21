#include "hal/hal_http_server.h"

#ifdef HAL_ENABLE_HTTP_SERVER

#include "hal/hal_tcp.h"

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
  const char *path;
  bool prefix;
  hal_http_handler_t handler;
  void *user;
} hal_http_route_t;

typedef struct {
  hal_tcp_socket_t socket;
  hal_net_endpoint_t remote;
  char request[HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE];
  size_t request_len;
} hal_http_client_t;

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

static bool send_all(hal_tcp_socket_t socket, const char *data, size_t len) {
  size_t sent = 0u;
  while (sent < len) {
    int rc = hal_tcp_socket_send(socket, data + sent, len - sent);
    if (rc <= 0) {
      return false;
    }
    sent += (size_t)rc;
  }
  return true;
}

static bool append_header(char *out, size_t out_size, size_t *pos,
                          const char *text) {
  size_t len = strlen(text);
  if (*pos + len >= out_size) {
    return false;
  }
  memcpy(out + *pos, text, len);
  *pos += len;
  out[*pos] = '\0';
  return true;
}

static char lower_ascii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool str_ieq(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  while (*a && *b) {
    if (lower_ascii(*a) != lower_ascii(*b)) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

static bool str_nieq(const char *a, const char *b, size_t len) {
  if (!a || !b) {
    return false;
  }
  for (size_t i = 0u; i < len; ++i) {
    if (a[i] == '\0' || b[i] == '\0') {
      return false;
    }
    if (lower_ascii(a[i]) != lower_ascii(b[i])) {
      return false;
    }
  }
  return true;
}

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

static bool send_response(hal_tcp_socket_t socket,
                          const hal_http_response_t *response,
                          bool suppress_body) {
  char header[HAL_HTTP_SERVER_RESPONSE_HEADER_SIZE];
  size_t pos = 0u;

  if (!append_header(header, sizeof(header), &pos, "HTTP/1.1 ") ||
      !append_uint(header, sizeof(header), &pos, response->status_code) ||
      !append_header(header, sizeof(header), &pos, " ") ||
      !append_header(header, sizeof(header), &pos, response->reason) ||
      !append_header(header, sizeof(header), &pos, "\r\nContent-Type: ") ||
      !append_header(header, sizeof(header), &pos, response->content_type) ||
      !append_header(header, sizeof(header), &pos, "\r\nContent-Length: ") ||
      !append_uint(header, sizeof(header), &pos,
                   (uint32_t)response->body_len)) {
    return false;
  }

  for (size_t i = 0u; i < response->header_count; ++i) {
    if (!append_header(header, sizeof(header), &pos, "\r\n") ||
        !append_header(header, sizeof(header), &pos,
                       response->headers[i].name) ||
        !append_header(header, sizeof(header), &pos, ": ") ||
        !append_header(header, sizeof(header), &pos,
                       response->headers[i].value)) {
      return false;
    }
  }

  if (!append_header(header, sizeof(header), &pos,
                     "\r\nConnection: close\r\n\r\n")) {
    return false;
  }

  const size_t body_len = suppress_body ? 0u : response->body_len;
  char packet[sizeof(header) + HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE];
  if (pos + body_len <= sizeof(packet)) {
    memcpy(packet, header, pos);
    if (body_len > 0u) {
      memcpy(packet + pos, response->body, body_len);
    }
    return send_all(socket, packet, pos + body_len);
  }

  if (!send_all(socket, header, pos)) {
    return false;
  }
  return body_len == 0u || send_all(socket, response->body, body_len);
}

static void send_response_and_close(hal_http_client_t *client,
                                    const hal_http_response_t *response,
                                    bool suppress_body) {
  send_response(client->socket, response, suppress_body);
  clear_client(client);
}

static void send_text_error_and_close(hal_http_client_t *client,
                                      uint16_t status_code, const char *reason,
                                      const char *body) {
  hal_http_response_t response;
  reset_response(&response);
  hal_http_response_set_status(&response, status_code, reason);
  hal_http_response_write_str(&response, body);
  send_response_and_close(client, &response, false);
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

static size_t parse_content_length(char *headers_start, char *body_start) {
  char *line = headers_start;
  while (line && line < body_start) {
    char *next = find_crlf(line);
    if (!next) {
      break;
    }
    *next = '\0';
    if (str_nieq(line, "Content-Length:", 15u)) {
      char *p = line + 15;
      while (*p == ' ' || *p == '\t') {
        ++p;
      }
      size_t value = 0u;
      while (*p >= '0' && *p <= '9') {
        value = value * 10u + (size_t)(*p - '0');
        ++p;
      }
      *next = '\r';
      return value;
    }
    *next = '\r';
    line = next + 2;
  }
  return 0u;
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

static bool request_complete(const hal_http_client_t *client) {
  const char *end = strstr(client->request, "\r\n\r\n");
  if (!end) {
    return false;
  }

  char tmp[HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE];
  memcpy(tmp, client->request, client->request_len + 1u);
  char *body = strstr(tmp, "\r\n\r\n");
  if (!body) {
    return false;
  }
  body += 4;
  size_t content_len = parse_content_length(tmp, body);
  size_t header_len = (size_t)(end - client->request) + 4u;
  return client->request_len >= header_len + content_len;
}

static void process_request(hal_http_client_t *client) {
  hal_http_response_t response;
  reset_response(&response);

  char *request_line_end = find_crlf(client->request);
  if (!request_line_end) {
    send_text_error_and_close(client, 400u, "Bad Request", "Bad Request\n");
    return;
  }

  *request_line_end = '\0';
  char *method_s = client->request;
  char *target = strchr(method_s, ' ');
  if (!target) {
    send_text_error_and_close(client, 400u, "Bad Request", "Bad Request\n");
    return;
  }
  *target++ = '\0';
  char *version = strchr(target, ' ');
  if (!version) {
    send_text_error_and_close(client, 400u, "Bad Request", "Bad Request\n");
    return;
  }
  *version++ = '\0';

  hal_http_method_t method = parse_method(method_s);
  char *query = strchr(target, '?');
  if (query) {
    *query++ = '\0';
  }

  char *body = strstr(request_line_end + 2, "\r\n\r\n");
  size_t body_len = 0u;
  hal_http_header_t headers[HAL_HTTP_SERVER_MAX_REQUEST_HEADERS];
  size_t header_count = 0u;
  if (body) {
    char *headers_start = request_line_end + 2;
    body += 4;
    body_len = parse_content_length(headers_start, body);
    header_count = parse_request_headers(headers_start, body, headers,
                                         HAL_HTTP_SERVER_MAX_REQUEST_HEADERS);
    body[body_len] = '\0';
  }

  if (method == HAL_HTTP_METHOD_UNKNOWN || strncmp(version, "HTTP/", 5) != 0 ||
      target[0] != '/') {
    send_text_error_and_close(client, 400u, "Bad Request", "Bad Request\n");
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

  send_response_and_close(client, &response, method == HAL_HTTP_METHOD_HEAD);
}

static hal_status_t route_common(hal_http_method_t method, const char *path,
                                 bool prefix, hal_http_handler_t handler,
                                 void *user) {
  if (method == HAL_HTTP_METHOD_UNKNOWN || !path || path[0] != '/' ||
      !handler) {
    return HAL_EINVAL;
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
      s_routes[i].path = path;
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
    }

    if (client->request_len + 1u >= sizeof(client->request)) {
      send_text_error_and_close(client, 413u, "Payload Too Large",
                                "Payload Too Large\n");
      continue;
    }

    if (request_complete(client)) {
      process_request(client);
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

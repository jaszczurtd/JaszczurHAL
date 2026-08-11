/** @file Target-neutral WebSocket service over HAL TCP. */
#include "hal/network/websocket/hal_websocket.h"
#include "hal/network/http/jh_http_text.h"

#ifdef HAL_ENABLE_WEBSOCKET

#include "hal/network/hal_tcp.h"
#include "hal/system/hal_system.h"

#include <string.h>

#define WS_OPCODE_CONTINUATION 0x0u
#define WS_OPCODE_TEXT 0x1u
#define WS_OPCODE_BINARY 0x2u
#define WS_OPCODE_CLOSE 0x8u
#define WS_OPCODE_PING 0x9u
#define WS_OPCODE_PONG 0xau

#define WS_CLOSE_NORMAL 1000u
#define WS_CLOSE_PROTOCOL_ERROR 1002u
#define WS_CLOSE_UNSUPPORTED_DATA 1003u
#define WS_CLOSE_TOO_BIG 1009u

typedef enum {
  WS_CLIENT_UNUSED = 0,
  WS_CLIENT_HANDSHAKE,
  WS_CLIENT_OPEN
} ws_client_state_t;

typedef struct {
  ws_client_state_t state;
  hal_tcp_socket_t socket;
  hal_net_endpoint_t remote;
  char request[HAL_WEBSOCKET_REQUEST_BUFFER_SIZE];
  size_t request_len;
  uint8_t frame[HAL_WEBSOCKET_FRAME_BUFFER_SIZE];
  size_t frame_len;
  uint32_t accepted_ms;
  uint32_t last_activity_ms;
} ws_client_t;

typedef struct {
  uint32_t state[5];
  uint64_t bit_len;
  uint8_t buffer[64];
  size_t buffer_len;
} ws_sha1_t;

static hal_tcp_listener_t s_listener;
static char s_path[HAL_WEBSOCKET_PATH_MAX] = "/ws";
static ws_client_t s_clients[HAL_WEBSOCKET_MAX_CLIENTS];
static hal_websocket_callbacks_t s_callbacks;
static void *s_callbacks_user;

static uint32_t rotl32(uint32_t value, uint8_t bits) {
  return (value << bits) | (value >> (32u - bits));
}

static uint32_t read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
         ((uint32_t)p[2] << 8u) | (uint32_t)p[3];
}

static void write_be32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24u);
  p[1] = (uint8_t)(value >> 16u);
  p[2] = (uint8_t)(value >> 8u);
  p[3] = (uint8_t)value;
}

static void ws_sha1_transform(ws_sha1_t *ctx, const uint8_t block[64]) {
  uint32_t w[80];
  for (size_t i = 0u; i < 16u; ++i) {
    w[i] = read_be32(block + (i * 4u));
  }
  for (size_t i = 16u; i < 80u; ++i) {
    w[i] = rotl32(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1u);
  }

  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];

  for (size_t i = 0u; i < 80u; ++i) {
    uint32_t f = 0u;
    uint32_t k = 0u;
    if (i < 20u) {
      f = (b & c) | ((~b) & d);
      k = 0x5a827999u;
    } else if (i < 40u) {
      f = b ^ c ^ d;
      k = 0x6ed9eba1u;
    } else if (i < 60u) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8f1bbcdcu;
    } else {
      f = b ^ c ^ d;
      k = 0xca62c1d6u;
    }

    uint32_t temp = rotl32(a, 5u) + f + e + k + w[i];
    e = d;
    d = c;
    c = rotl32(b, 30u);
    b = a;
    a = temp;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
}

static void ws_sha1_init(ws_sha1_t *ctx) {
  ctx->state[0] = 0x67452301u;
  ctx->state[1] = 0xefcdab89u;
  ctx->state[2] = 0x98badcfeu;
  ctx->state[3] = 0x10325476u;
  ctx->state[4] = 0xc3d2e1f0u;
  ctx->bit_len = 0u;
  ctx->buffer_len = 0u;
}

static void ws_sha1_update(ws_sha1_t *ctx, const uint8_t *data, size_t len) {
  if (len == 0u) {
    return;
  }
  ctx->bit_len += (uint64_t)len * 8u;
  while (len > 0u) {
    size_t space = sizeof(ctx->buffer) - ctx->buffer_len;
    size_t take = len < space ? len : space;
    memcpy(ctx->buffer + ctx->buffer_len, data, take);
    ctx->buffer_len += take;
    data += take;
    len -= take;
    if (ctx->buffer_len == sizeof(ctx->buffer)) {
      ws_sha1_transform(ctx, ctx->buffer);
      ctx->buffer_len = 0u;
    }
  }
}

static void ws_sha1_final(ws_sha1_t *ctx, uint8_t digest[20]) {
  const uint64_t original_bit_len = ctx->bit_len;
  uint8_t pad = 0x80u;
  ws_sha1_update(ctx, &pad, 1u);
  uint8_t zero = 0u;
  while (ctx->buffer_len != 56u) {
    if (ctx->buffer_len == sizeof(ctx->buffer)) {
      ws_sha1_transform(ctx, ctx->buffer);
      ctx->buffer_len = 0u;
    }
    ws_sha1_update(ctx, &zero, 1u);
  }

  uint8_t len_be[8];
  for (size_t i = 0u; i < sizeof(len_be); ++i) {
    len_be[7u - i] = (uint8_t)(original_bit_len >> (i * 8u));
  }
  ws_sha1_update(ctx, len_be, sizeof(len_be));

  for (size_t i = 0u; i < 5u; ++i) {
    write_be32(digest + (i * 4u), ctx->state[i]);
  }
}

static bool base64_encode_20(const uint8_t input[20], char output[29]) {
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out = 0u;
  for (size_t i = 0u; i < 18u; i += 3u) {
    uint32_t v = ((uint32_t)input[i] << 16u) | ((uint32_t)input[i + 1u] << 8u) |
                 input[i + 2u];
    output[out++] = table[(v >> 18u) & 0x3fu];
    output[out++] = table[(v >> 12u) & 0x3fu];
    output[out++] = table[(v >> 6u) & 0x3fu];
    output[out++] = table[v & 0x3fu];
  }

  uint32_t v = ((uint32_t)input[18] << 16u) | ((uint32_t)input[19] << 8u);
  output[out++] = table[(v >> 18u) & 0x3fu];
  output[out++] = table[(v >> 12u) & 0x3fu];
  output[out++] = table[(v >> 6u) & 0x3fu];
  output[out++] = '=';
  output[out] = '\0';
  return out == 28u;
}

#define send_all jh_http_send_all
#define append_text jh_http_append_text
#define lower_ascii jh_http_lower_ascii
#define str_ieq jh_http_str_ieq

static bool str_nieq(const char *a, const char *b, size_t len) {
  for (size_t i = 0u; i < len; ++i) {
    if (lower_ascii(a[i]) != lower_ascii(b[i])) {
      return false;
    }
  }
  return true;
}

static bool str_icontains(const char *text, const char *needle) {
  if (!text || !needle || !*needle) {
    return false;
  }
  for (const char *p = text; *p; ++p) {
    const char *a = p;
    const char *b = needle;
    while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) {
      ++a;
      ++b;
    }
    if (*b == '\0') {
      return true;
    }
  }
  return false;
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

static bool copy_header_value(const char *headers, const char *name, char *out,
                              size_t out_size) {
  if (!headers || !name || !out || out_size == 0u) {
    return false;
  }
  out[0] = '\0';
  const size_t name_len = strlen(name);

  const char *line = headers;
  while (*line) {
    const char *next = strstr(line, "\r\n");
    size_t line_len = next ? (size_t)(next - line) : strlen(line);
    if (line_len == 0u) {
      return false;
    }

    const char *colon = NULL;
    for (size_t i = 0u; i < line_len; ++i) {
      if (line[i] == ':') {
        colon = line + i;
        break;
      }
    }
    if (colon && (size_t)(colon - line) == name_len &&
        str_nieq(line, name, name_len)) {
      const char *value = colon + 1;
      const char *line_end = line + line_len;
      while (value < line_end && (*value == ' ' || *value == '\t')) {
        ++value;
      }
      while (line_end > value &&
             (line_end[-1] == ' ' || line_end[-1] == '\t')) {
        --line_end;
      }
      size_t value_len = (size_t)(line_end - value);
      if (value_len >= out_size) {
        return false;
      }
      memcpy(out, value, value_len);
      out[value_len] = '\0';
      return true;
    }

    if (!next) {
      break;
    }
    line = next + 2;
  }
  return false;
}

static void compute_accept_key(const char *client_key, char out[29]) {
  static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t digest[20];
  ws_sha1_t sha1;
  ws_sha1_init(&sha1);
  ws_sha1_update(&sha1, (const uint8_t *)client_key, strlen(client_key));
  ws_sha1_update(&sha1, (const uint8_t *)guid, strlen(guid));
  ws_sha1_final(&sha1, digest);
  base64_encode_20(digest, out);
}

static void clear_client(ws_client_t *client, uint16_t code,
                         bool notify_disconnect) {
  hal_websocket_client_t id = (hal_websocket_client_t)(client - &s_clients[0]);
  bool was_open = client->state == WS_CLIENT_OPEN;
  if (client->socket) {
    hal_tcp_socket_close(client->socket);
  }
  memset(client, 0, sizeof(*client));
  client->remote.family = HAL_NET_AF_UNSPEC;
  if (notify_disconnect && was_open && s_callbacks.on_disconnect) {
    s_callbacks.on_disconnect(id, code, s_callbacks_user);
  }
}

static bool send_http_error(hal_tcp_socket_t socket, const char *status) {
  char response[128];
  size_t pos = 0u;
  return append_text(response, sizeof(response), &pos, "HTTP/1.1 ") &&
         append_text(response, sizeof(response), &pos, status) &&
         append_text(response, sizeof(response), &pos,
                     "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n") &&
         send_all(socket, response, pos);
}

static bool send_handshake_response(hal_tcp_socket_t socket, const char *key) {
  char accept[29];
  char response[192];
  size_t pos = 0u;
  compute_accept_key(key, accept);
  return append_text(response, sizeof(response), &pos,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: ") &&
         append_text(response, sizeof(response), &pos, accept) &&
         append_text(response, sizeof(response), &pos, "\r\n\r\n") &&
         send_all(socket, response, pos);
}

static bool request_complete(const ws_client_t *client) {
  return strstr(client->request, "\r\n\r\n") != NULL;
}

static bool process_handshake(ws_client_t *client) {
  char *request_line_end = find_crlf(client->request);
  if (!request_line_end) {
    send_http_error(client->socket, "400 Bad Request");
    return false;
  }

  *request_line_end = '\0';
  char *method = client->request;
  char *target = strchr(method, ' ');
  if (!target) {
    send_http_error(client->socket, "400 Bad Request");
    return false;
  }
  *target++ = '\0';
  char *version = strchr(target, ' ');
  if (!version) {
    send_http_error(client->socket, "400 Bad Request");
    return false;
  }
  *version++ = '\0';

  char *query = strchr(target, '?');
  if (query) {
    *query = '\0';
  }

  if (!str_ieq(method, "GET") || strcmp(target, s_path) != 0 ||
      strncmp(version, "HTTP/", 5) != 0) {
    send_http_error(client->socket, "404 Not Found");
    return false;
  }

  char *headers = request_line_end + 2;
  char upgrade[16];
  char connection[64];
  char key[64];
  char version_header[8];
  if (!copy_header_value(headers, "Upgrade", upgrade, sizeof(upgrade)) ||
      !copy_header_value(headers, "Connection", connection,
                         sizeof(connection)) ||
      !copy_header_value(headers, "Sec-WebSocket-Key", key, sizeof(key)) ||
      !copy_header_value(headers, "Sec-WebSocket-Version", version_header,
                         sizeof(version_header)) ||
      !str_ieq(upgrade, "websocket") || !str_icontains(connection, "upgrade") ||
      strcmp(version_header, "13") != 0 || strlen(key) == 0u) {
    send_http_error(client->socket, "400 Bad Request");
    return false;
  }

  if (!send_handshake_response(client->socket, key)) {
    return false;
  }
  client->state = WS_CLIENT_OPEN;
  client->request_len = 0u;
  if (s_callbacks.on_connect) {
    hal_websocket_client_t id =
        (hal_websocket_client_t)(client - &s_clients[0]);
    s_callbacks.on_connect(id, s_callbacks_user);
  }
  return true;
}

static hal_status_t send_frame(hal_tcp_socket_t socket, uint8_t opcode,
                               const void *data, size_t len) {
  if (len > HAL_WEBSOCKET_MAX_PAYLOAD_SIZE) {
    return HAL_EOVERFLOW;
  }

  uint8_t packet[10u + HAL_WEBSOCKET_MAX_PAYLOAD_SIZE];
  size_t pos = 0u;
  packet[pos++] = (uint8_t)(0x80u | (opcode & 0x0fu));
  if (len <= 125u) {
    packet[pos++] = (uint8_t)len;
  } else if (len <= 65535u) {
    packet[pos++] = 126u;
    packet[pos++] = (uint8_t)(len >> 8u);
    packet[pos++] = (uint8_t)len;
  } else {
    return HAL_EOVERFLOW;
  }
  if (len > 0u) {
    if (!data) {
      return HAL_EINVAL;
    }
    memcpy(packet + pos, data, len);
    pos += len;
  }
  return send_all(socket, packet, pos) ? HAL_OK : HAL_EIO;
}

static void send_close_frame(ws_client_t *client, uint16_t code) {
  uint8_t payload[2] = {(uint8_t)(code >> 8u), (uint8_t)code};
  if (client->socket && hal_tcp_socket_is_connected(client->socket)) {
    send_frame(client->socket, WS_OPCODE_CLOSE, payload, sizeof(payload));
  }
}

static bool parse_frame(ws_client_t *client) {
  if (client->frame_len < 2u) {
    return false;
  }

  const uint8_t b0 = client->frame[0];
  const uint8_t b1 = client->frame[1];
  const bool fin = (b0 & 0x80u) != 0u;
  const bool reserved = (b0 & 0x70u) != 0u;
  const uint8_t opcode = b0 & 0x0fu;
  const bool masked = (b1 & 0x80u) != 0u;
  uint64_t payload_len = (uint64_t)(b1 & 0x7fu);
  size_t pos = 2u;

  if (!fin || reserved || !masked || opcode == WS_OPCODE_CONTINUATION) {
    send_close_frame(client, WS_CLOSE_PROTOCOL_ERROR);
    clear_client(client, WS_CLOSE_PROTOCOL_ERROR, true);
    return false;
  }
  if (payload_len == 126u) {
    if (client->frame_len < 4u) {
      return false;
    }
    payload_len = ((uint64_t)client->frame[2] << 8u) | client->frame[3];
    pos = 4u;
  } else if (payload_len == 127u) {
    send_close_frame(client, WS_CLOSE_TOO_BIG);
    clear_client(client, WS_CLOSE_TOO_BIG, true);
    return false;
  }
  const bool control_frame = opcode >= WS_OPCODE_CLOSE;
  if (payload_len > HAL_WEBSOCKET_MAX_PAYLOAD_SIZE ||
      (control_frame && payload_len > 125u)) {
    send_close_frame(client, WS_CLOSE_TOO_BIG);
    clear_client(client, WS_CLOSE_TOO_BIG, true);
    return false;
  }
  if (client->frame_len < pos + 4u + (size_t)payload_len) {
    return false;
  }

  uint8_t mask[4];
  memcpy(mask, client->frame + pos, sizeof(mask));
  pos += sizeof(mask);
  uint8_t payload[HAL_WEBSOCKET_MAX_PAYLOAD_SIZE];
  for (size_t i = 0u; i < (size_t)payload_len; ++i) {
    payload[i] = (uint8_t)(client->frame[pos + i] ^ mask[i & 3u]);
  }

  const size_t total = pos + (size_t)payload_len;
  const size_t remaining = client->frame_len - total;
  if (remaining > 0u) {
    memmove(client->frame, client->frame + total, remaining);
  }
  client->frame_len = remaining;

  if (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY) {
    if (s_callbacks.on_message) {
      hal_websocket_client_t id =
          (hal_websocket_client_t)(client - &s_clients[0]);
      s_callbacks.on_message(id,
                             opcode == WS_OPCODE_TEXT
                                 ? HAL_WEBSOCKET_MESSAGE_TEXT
                                 : HAL_WEBSOCKET_MESSAGE_BINARY,
                             payload, (size_t)payload_len, s_callbacks_user);
      if (client->state != WS_CLIENT_OPEN || !s_listener) {
        return false;
      }
    }
  } else if (opcode == WS_OPCODE_PING) {
    send_frame(client->socket, WS_OPCODE_PONG, payload, (size_t)payload_len);
  } else if (opcode == WS_OPCODE_CLOSE) {
    if (payload_len == 1u) {
      send_close_frame(client, WS_CLOSE_PROTOCOL_ERROR);
      clear_client(client, WS_CLOSE_PROTOCOL_ERROR, true);
      return false;
    }
    uint16_t code = WS_CLOSE_NORMAL;
    if (payload_len >= 2u) {
      code = ((uint16_t)payload[0] << 8u) | payload[1];
    }
    send_close_frame(client, code);
    clear_client(client, code, true);
    return false;
  } else if (opcode != WS_OPCODE_PONG) {
    send_close_frame(client, WS_CLOSE_UNSUPPORTED_DATA);
    clear_client(client, WS_CLOSE_UNSUPPORTED_DATA, true);
    return false;
  }

  return true;
}

static void poll_open_client(ws_client_t *client) {
  if (!hal_tcp_socket_is_connected(client->socket)) {
    clear_client(client, WS_CLOSE_NORMAL, true);
    return;
  }

  if (hal_tcp_socket_can_recv(client->socket) &&
      client->frame_len < sizeof(client->frame)) {
    int rc =
        hal_tcp_socket_recv(client->socket, client->frame + client->frame_len,
                            sizeof(client->frame) - client->frame_len, 0u);
    if (rc < 0) {
      clear_client(client, WS_CLOSE_NORMAL, true);
      return;
    }
    client->frame_len += (size_t)rc;
  }

  while (client->state == WS_CLIENT_OPEN && parse_frame(client)) {
  }
  if (client->state == WS_CLIENT_OPEN &&
      client->frame_len >= sizeof(client->frame)) {
    send_close_frame(client, WS_CLOSE_TOO_BIG);
    clear_client(client, WS_CLOSE_TOO_BIG, true);
  }
}

static void poll_handshake_client(ws_client_t *client) {
  if (!hal_tcp_socket_is_connected(client->socket)) {
    clear_client(client, WS_CLOSE_NORMAL, false);
    return;
  }

  if (hal_tcp_socket_can_recv(client->socket) &&
      client->request_len + 1u < sizeof(client->request)) {
    int rc = hal_tcp_socket_recv(
        client->socket, client->request + client->request_len,
        sizeof(client->request) - client->request_len - 1u, 0u);
    if (rc < 0) {
      clear_client(client, WS_CLOSE_NORMAL, false);
      return;
    }
    client->request_len += (size_t)rc;
    client->request[client->request_len] = '\0';
    if (rc > 0) {
      client->last_activity_ms = hal_millis();
    }
  }

  if (client->request_len + 1u >= sizeof(client->request)) {
    send_http_error(client->socket, "413 Payload Too Large");
    clear_client(client, WS_CLOSE_TOO_BIG, false);
    return;
  }

  if (request_complete(client) && !process_handshake(client)) {
    clear_client(client, WS_CLOSE_PROTOCOL_ERROR, false);
    return;
  }

  if (client->state == WS_CLIENT_HANDSHAKE) {
    const uint32_t now_ms = hal_millis();
    const bool first_byte_timeout = client->request_len == 0u &&
                                    HAL_WEBSOCKET_FIRST_BYTE_TIMEOUT_MS > 0u &&
                                    (uint32_t)(now_ms - client->accepted_ms) >=
                                        HAL_WEBSOCKET_FIRST_BYTE_TIMEOUT_MS;
    const bool handshake_timeout = HAL_WEBSOCKET_HANDSHAKE_TIMEOUT_MS > 0u &&
                                   (uint32_t)(now_ms - client->accepted_ms) >=
                                       HAL_WEBSOCKET_HANDSHAKE_TIMEOUT_MS;
    const bool idle_timeout = client->request_len > 0u &&
                              HAL_WEBSOCKET_HANDSHAKE_IDLE_TIMEOUT_MS > 0u &&
                              (uint32_t)(now_ms - client->last_activity_ms) >=
                                  HAL_WEBSOCKET_HANDSHAKE_IDLE_TIMEOUT_MS;
    if (first_byte_timeout || handshake_timeout || idle_timeout) {
      send_http_error(client->socket, "408 Request Timeout");
      clear_client(client, WS_CLOSE_PROTOCOL_ERROR, false);
    }
  }
}

hal_status_t
hal_websocket_server_set_callbacks(const hal_websocket_callbacks_t *callbacks,
                                   void *user) {
  if (!callbacks) {
    memset(&s_callbacks, 0, sizeof(s_callbacks));
    s_callbacks_user = NULL;
    return HAL_OK;
  }
  s_callbacks = *callbacks;
  s_callbacks_user = user;
  return HAL_OK;
}

hal_status_t hal_websocket_server_start(uint16_t port, const char *path) {
  if (s_listener) {
    return HAL_OK;
  }
  if (port == 0u || !path || path[0] != '/') {
    return HAL_EINVAL;
  }
  const size_t path_len = strlen(path);
  if (path_len >= HAL_WEBSOCKET_PATH_MAX) {
    return HAL_EOVERFLOW;
  }

  s_listener = hal_tcp_listener_open();
  if (!s_listener) {
    return HAL_ENOMEM;
  }

  hal_net_endpoint_t local = {};
  local.family = HAL_NET_AF_INET;
  local.addr_len = HAL_NET_IPV4_ADDR_LEN;
  local.port = port;

  if (!hal_tcp_listener_bind(s_listener, &local) ||
      !hal_tcp_listener_listen(s_listener, HAL_WEBSOCKET_DEFAULT_BACKLOG)) {
    hal_tcp_listener_close(s_listener);
    s_listener = NULL;
    return HAL_EIO;
  }

  memcpy(s_path, path, path_len + 1u);
  return HAL_OK;
}

void hal_websocket_server_stop(void) {
  for (size_t i = 0u; i < HAL_WEBSOCKET_MAX_CLIENTS; ++i) {
    clear_client(&s_clients[i], WS_CLOSE_NORMAL, true);
  }
  if (s_listener) {
    hal_tcp_listener_close(s_listener);
    s_listener = NULL;
  }
}

bool hal_websocket_server_is_running(void) { return s_listener != NULL; }

void hal_websocket_server_poll(void) {
  if (!s_listener) {
    return;
  }

  while (hal_tcp_listener_can_accept(s_listener)) {
    ws_client_t *slot = NULL;
    for (size_t i = 0u; i < HAL_WEBSOCKET_MAX_CLIENTS; ++i) {
      if (s_clients[i].state == WS_CLIENT_UNUSED) {
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
      send_http_error(socket, "503 Service Unavailable");
      hal_tcp_socket_close(socket);
      break;
    }

    memset(slot, 0, sizeof(*slot));
    slot->state = WS_CLIENT_HANDSHAKE;
    slot->socket = socket;
    slot->remote = remote;
    slot->accepted_ms = hal_millis();
    slot->last_activity_ms = slot->accepted_ms;
  }

  for (size_t i = 0u; i < HAL_WEBSOCKET_MAX_CLIENTS; ++i) {
    ws_client_t *client = &s_clients[i];
    if (client->state == WS_CLIENT_HANDSHAKE) {
      poll_handshake_client(client);
    } else if (client->state == WS_CLIENT_OPEN) {
      poll_open_client(client);
    }
  }
}

size_t hal_websocket_client_count(void) {
  size_t count = 0u;
  for (size_t i = 0u; i < HAL_WEBSOCKET_MAX_CLIENTS; ++i) {
    if (s_clients[i].state == WS_CLIENT_OPEN) {
      ++count;
    }
  }
  return count;
}

bool hal_websocket_client_is_connected(hal_websocket_client_t client) {
  return client < HAL_WEBSOCKET_MAX_CLIENTS &&
         s_clients[client].state == WS_CLIENT_OPEN &&
         hal_tcp_socket_is_connected(s_clients[client].socket);
}

hal_status_t hal_websocket_send(hal_websocket_client_t client,
                                hal_websocket_message_type_t type,
                                const void *data, size_t len) {
  if (client >= HAL_WEBSOCKET_MAX_CLIENTS ||
      s_clients[client].state != WS_CLIENT_OPEN) {
    return HAL_ENOENT;
  }
  if (!hal_tcp_socket_is_connected(s_clients[client].socket)) {
    return HAL_ESTATE;
  }
  uint8_t opcode = 0u;
  if (type == HAL_WEBSOCKET_MESSAGE_TEXT) {
    opcode = WS_OPCODE_TEXT;
  } else if (type == HAL_WEBSOCKET_MESSAGE_BINARY) {
    opcode = WS_OPCODE_BINARY;
  } else {
    return HAL_EINVAL;
  }
  return send_frame(s_clients[client].socket, opcode, data, len);
}

hal_status_t hal_websocket_send_text(hal_websocket_client_t client,
                                     const char *text) {
  return text ? hal_websocket_send(client, HAL_WEBSOCKET_MESSAGE_TEXT, text,
                                   strlen(text))
              : HAL_EINVAL;
}

hal_status_t hal_websocket_broadcast(hal_websocket_message_type_t type,
                                     const void *data, size_t len,
                                     size_t *sent_count) {
  if (sent_count) {
    *sent_count = 0u;
  }
  if (type != HAL_WEBSOCKET_MESSAGE_TEXT &&
      type != HAL_WEBSOCKET_MESSAGE_BINARY) {
    return HAL_EINVAL;
  }
  if (len > 0u && !data) {
    return HAL_EINVAL;
  }
  if (len > HAL_WEBSOCKET_MAX_PAYLOAD_SIZE) {
    return HAL_EOVERFLOW;
  }

  size_t sent = 0u;
  hal_status_t first_error = HAL_OK;
  for (hal_websocket_client_t i = 0u; i < HAL_WEBSOCKET_MAX_CLIENTS; ++i) {
    if (s_clients[i].state != WS_CLIENT_OPEN) {
      continue;
    }
    hal_status_t status = hal_websocket_send(i, type, data, len);
    if (status == HAL_OK) {
      ++sent;
    } else if (first_error == HAL_OK) {
      first_error = status;
    }
  }
  if (sent_count) {
    *sent_count = sent;
  }
  return first_error;
}

hal_status_t hal_websocket_broadcast_text(const char *text,
                                          size_t *sent_count) {
  if (sent_count) {
    *sent_count = 0u;
  }
  return text ? hal_websocket_broadcast(HAL_WEBSOCKET_MESSAGE_TEXT, text,
                                        strlen(text), sent_count)
              : HAL_EINVAL;
}

hal_status_t hal_websocket_close(hal_websocket_client_t client,
                                 uint16_t close_code) {
  if (!hal_websocket_client_is_connected(client)) {
    return HAL_ENOENT;
  }
  send_close_frame(&s_clients[client], close_code);
  clear_client(&s_clients[client], close_code, true);
  return HAL_OK;
}

#endif /* HAL_ENABLE_WEBSOCKET */

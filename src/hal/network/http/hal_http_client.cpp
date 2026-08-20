#include "hal/network/http/hal_http_client.h"

#ifdef HAL_ENABLE_HTTP_CLIENT

#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/system/hal_system.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <strings.h>

#define JH_HTTP_CLIENT_REQUEST_HEADER_MAX 1536u
#define JH_HTTP_CLIENT_RESPONSE_HEADER_MAX 2048u

typedef struct {
  hal_http_client_transport_t kind;
  hal_tcp_socket_t tcp;
#ifdef HAL_ENABLE_TLS
  hal_tls_client_t tls;
#endif
} jh_http_stream_t;

typedef struct {
  char request_headers[JH_HTTP_CLIENT_REQUEST_HEADER_MAX];
  char response_headers[JH_HTTP_CLIENT_RESPONSE_HEADER_MAX];
  uint8_t chunk[512];
} jh_http_buffers_t;

static bool token_is_valid(const char *value) {
  return value != NULL && value[0] != '\0' && strchr(value, '\r') == NULL &&
         strchr(value, '\n') == NULL;
}

static hal_status_t stream_close(jh_http_stream_t *stream,
                                 uint32_t timeout_ms) {
#ifdef HAL_ENABLE_TLS
  if (stream->tls != NULL) {
    hal_status_t status = hal_tls_client_shutdown_ex(stream->tls);
    const uint32_t started = hal_millis();
    while (status == HAL_EAGAIN &&
           (uint32_t)(hal_millis() - started) < timeout_ms) {
      status = hal_tls_client_poll_ex(stream->tls);
      if (status == HAL_EAGAIN) {
        hal_idle();
        hal_delay_ms(1u);
      }
    }
    (void)hal_tls_client_close_ex(stream->tls);
    stream->tls = NULL;
  }
#else
  (void)timeout_ms;
#endif
  if (stream->tcp != NULL) {
    hal_tcp_socket_close(stream->tcp);
    stream->tcp = NULL;
  }
  return HAL_OK;
}

#ifdef HAL_ENABLE_TLS
static hal_status_t wait_tls_connected(hal_tls_client_t client,
                                       uint32_t timeout_ms) {
  const uint32_t started = hal_millis();
  while ((uint32_t)(hal_millis() - started) < timeout_ms) {
    hal_tls_state_t state = HAL_TLS_STATE_FAILED;
    hal_status_t status = hal_tls_client_get_state_ex(client, &state);
    if (status != HAL_OK) {
      return status;
    }
    if (state == HAL_TLS_STATE_CONNECTED) {
      return HAL_OK;
    }
    if (state == HAL_TLS_STATE_FAILED || state == HAL_TLS_STATE_CLOSED) {
      hal_status_t last = HAL_EPROTO;
      int32_t provider_error = 0;
      (void)hal_tls_client_get_last_error_ex(client, &last, &provider_error);
      return last;
    }
    status = hal_tls_client_poll_ex(client);
    if (status != HAL_OK && status != HAL_EAGAIN) {
      return status;
    }
    hal_idle();
    hal_delay_ms(1u);
  }
  return HAL_ETIMEOUT;
}
#endif

static hal_status_t stream_connect(const hal_http_client_request_t *request,
                                   jh_http_stream_t *stream) {
  memset(stream, 0, sizeof(*stream));
  stream->kind = request->transport;
  if (request->transport == HAL_HTTP_CLIENT_TRANSPORT_TLS) {
#ifdef HAL_ENABLE_TLS
    if (request->tls_security == NULL) {
      return HAL_ECONFIG;
    }
    hal_tls_client_config_t config = {};
    hal_status_t status = hal_tls_client_config_init(&config);
    if (status == HAL_OK) {
      config.execution_model = HAL_TLS_EXECUTION_BOUNDED_WORKER;
      config.transport_timeout_ms = request->timeout_ms;
      config.operation_timeout_ms = request->timeout_ms;
      status = hal_tls_client_create_ex(&config, &stream->tls);
    }
    if (status == HAL_OK) {
      status = hal_tls_client_configure_server_ex(stream->tls, request->host,
                                                  request->port);
    }
    if (status == HAL_OK) {
      status = hal_tls_client_configure_security_ex(stream->tls,
                                                    request->tls_security);
    }
    if (status == HAL_OK) {
      status = hal_tls_client_connect_ex(stream->tls);
    }
    if (status == HAL_EAGAIN) {
      status = wait_tls_connected(stream->tls, request->timeout_ms);
    }
    return status;
#else
    return HAL_EUNSUPPORTED;
#endif
  }

  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.port = request->port;
  hal_status_t status = hal_net_resolve_ipv4_ex(request->host, endpoint.addr);
  if (status == HAL_OK) {
    status = hal_tcp_socket_open_ex(&stream->tcp);
  }
  if (status == HAL_OK) {
    status =
        hal_tcp_socket_connect_ex(stream->tcp, &endpoint, request->timeout_ms);
  }
  return status;
}

static hal_status_t stream_write_all(jh_http_stream_t *stream,
                                     const uint8_t *data, size_t length,
                                     uint32_t timeout_ms) {
  const uint32_t started = hal_millis();
  size_t offset = 0u;
  while (offset < length) {
    size_t written = 0u;
    const hal_status_t status =
#ifdef HAL_ENABLE_TLS
        stream->kind == HAL_HTTP_CLIENT_TRANSPORT_TLS
            ? hal_tls_client_write_ex(stream->tls, data + offset,
                                      length - offset, &written)
            :
#endif
            hal_tcp_socket_send_ex(stream->tcp, data + offset, length - offset,
                                   &written);
    offset += written;
    if (status != HAL_OK && status != HAL_EAGAIN) {
      return status;
    }
    if ((uint32_t)(hal_millis() - started) >= timeout_ms) {
      return HAL_ETIMEOUT;
    }
    if (offset < length) {
      hal_idle();
      hal_delay_ms(1u);
    }
  }
  return HAL_OK;
}

static hal_status_t stream_read(jh_http_stream_t *stream, uint8_t *data,
                                size_t capacity, size_t *received,
                                bool *closed) {
  *received = 0u;
  *closed = false;
#ifdef HAL_ENABLE_TLS
  if (stream->kind == HAL_HTTP_CLIENT_TRANSPORT_TLS) {
    hal_status_t status =
        hal_tls_client_read_ex(stream->tls, data, capacity, received);
    hal_tls_state_t state = HAL_TLS_STATE_FAILED;
    if (hal_tls_client_get_state_ex(stream->tls, &state) == HAL_OK &&
        state == HAL_TLS_STATE_CLOSED) {
      *closed = true;
    }
    return status;
  }
#endif
  const hal_status_t status =
      hal_tcp_socket_recv_ex(stream->tcp, data, capacity, 0u, received);
  *closed = !hal_tcp_socket_is_connected(stream->tcp);
  return status;
}

static hal_status_t append_header(char *buffer, size_t capacity, size_t *used,
                                  const char *format, const char *name,
                                  const char *value) {
  const int count =
      snprintf(buffer + *used, capacity - *used, format, name, value);
  if (count < 0 || (size_t)count >= capacity - *used) {
    return HAL_EOVERFLOW;
  }
  *used += (size_t)count;
  return HAL_OK;
}

static const char *find_header_value(const char *headers, const char *name) {
  const size_t name_length = strlen(name);
  const char *line = strstr(headers, "\r\n") + 2;
  while (line != NULL && line[0] != '\r') {
    const char *end = strstr(line, "\r\n");
    if (end == NULL) {
      return NULL;
    }
    if ((size_t)(end - line) > name_length && line[name_length] == ':' &&
        strncasecmp(line, name, name_length) == 0) {
      const char *value = line + name_length + 1u;
      while (value < end && *value == ' ') {
        ++value;
      }
      return value;
    }
    line = end + 2;
  }
  return NULL;
}

static bool parse_size_header_value(const char *value, size_t *out_value) {
  if (value == NULL || out_value == NULL) {
    return false;
  }
  while (*value == ' ' || *value == '\t') {
    ++value;
  }
  if (*value < '0' || *value > '9') {
    return false;
  }

  size_t parsed = 0u;
  do {
    const size_t digit = (size_t)(*value - '0');
    if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
    ++value;
  } while (*value >= '0' && *value <= '9');

  while (*value == ' ' || *value == '\t') {
    ++value;
  }
  if (value[0] != '\r' || value[1] != '\n') {
    return false;
  }
  *out_value = parsed;
  return true;
}

hal_status_t hal_http_client_request_init(hal_http_client_request_t *request) {
  if (request == NULL) {
    return HAL_EINVAL;
  }
  memset(request, 0, sizeof(*request));
  request->transport = HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT;
  request->port = 80u;
  request->method = "GET";
  request->path = "/";
  request->timeout_ms = 15000u;
  return HAL_OK;
}

hal_status_t
hal_http_client_perform_ex(const hal_http_client_request_t *request,
                           void *response_body, size_t response_body_capacity,
                           hal_http_client_response_t *out_response) {
  if (request == NULL || out_response == NULL ||
      (response_body_capacity > 0u && response_body == NULL) ||
      !token_is_valid(request->host) || !token_is_valid(request->method) ||
      request->path == NULL || request->path[0] != '/' ||
      strchr(request->path, '\r') != NULL ||
      strchr(request->path, '\n') != NULL || request->port == 0u ||
      request->timeout_ms == 0u ||
      (request->body_length > 0u && request->body == NULL) ||
      (request->header_count > 0u && request->headers == NULL) ||
      (request->transport != HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT &&
       request->transport != HAL_HTTP_CLIENT_TRANSPORT_TLS)) {
    return HAL_EINVAL;
  }
  memset(out_response, 0, sizeof(*out_response));

  std::unique_ptr<jh_http_buffers_t> buffers(new (std::nothrow)
                                                 jh_http_buffers_t{});
  if (!buffers) {
    return HAL_ENOMEM;
  }
  char *const request_headers = buffers->request_headers;
  size_t header_used = 0u;
  int count = snprintf(request_headers, JH_HTTP_CLIENT_REQUEST_HEADER_MAX,
                       "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n",
                       request->method, request->path, request->host);
  if (count < 0 || (size_t)count >= JH_HTTP_CLIENT_REQUEST_HEADER_MAX) {
    return HAL_EOVERFLOW;
  }
  header_used = (size_t)count;
  for (size_t index = 0u; index < request->header_count; ++index) {
    if (!token_is_valid(request->headers[index].name) ||
        !token_is_valid(request->headers[index].value)) {
      return HAL_EINVAL;
    }
    hal_status_t status =
        append_header(request_headers, JH_HTTP_CLIENT_REQUEST_HEADER_MAX,
                      &header_used, "%s: %s\r\n", request->headers[index].name,
                      request->headers[index].value);
    if (status != HAL_OK) {
      return status;
    }
  }
  if (request->body_length > 0u) {
    char length_text[24] = {};
    (void)snprintf(length_text, sizeof(length_text), "%zu",
                   request->body_length);
    hal_status_t status = append_header(
        request_headers, JH_HTTP_CLIENT_REQUEST_HEADER_MAX, &header_used,
        "%s: %s\r\n", "Content-Length", length_text);
    if (status != HAL_OK) {
      return status;
    }
  }
  if (header_used + 2u > JH_HTTP_CLIENT_REQUEST_HEADER_MAX) {
    return HAL_EOVERFLOW;
  }
  request_headers[header_used++] = '\r';
  request_headers[header_used++] = '\n';

  jh_http_stream_t stream = {};
  hal_status_t status = stream_connect(request, &stream);
  if (status == HAL_OK) {
    status = stream_write_all(
        &stream, reinterpret_cast<const uint8_t *>(request_headers),
        header_used, request->timeout_ms);
  }
  if (status == HAL_OK && request->body_length > 0u) {
    status =
        stream_write_all(&stream, static_cast<const uint8_t *>(request->body),
                         request->body_length, request->timeout_ms);
  }

  char *const response_headers = buffers->response_headers;
  size_t response_header_used = 0u;
  size_t copied_body = 0u;
  size_t total_body = 0u;
  bool headers_complete = false;
  bool closed = false;
  const uint32_t started = hal_millis();
  while (status == HAL_OK && !closed &&
         (uint32_t)(hal_millis() - started) < request->timeout_ms) {
    uint8_t *const chunk = buffers->chunk;
    size_t received = 0u;
    status =
        stream_read(&stream, chunk, sizeof(buffers->chunk), &received, &closed);
    if (status == HAL_EAGAIN) {
      status = HAL_OK;
      hal_idle();
      hal_delay_ms(1u);
      continue;
    }
    size_t offset = 0u;
    if (status == HAL_OK && !headers_complete) {
      while (offset < received && !headers_complete) {
        if (response_header_used + 1u >= JH_HTTP_CLIENT_RESPONSE_HEADER_MAX) {
          status = HAL_EOVERFLOW;
          break;
        }
        response_headers[response_header_used++] = (char)chunk[offset++];
        response_headers[response_header_used] = '\0';
        headers_complete = response_header_used >= 4u &&
                           memcmp(response_headers + response_header_used - 4u,
                                  "\r\n\r\n", 4u) == 0;
      }
      if (headers_complete) {
        unsigned code = 0u;
        if (sscanf(response_headers, "HTTP/%*u.%*u %u", &code) != 1 ||
            code > UINT16_MAX) {
          status = HAL_EPROTO;
        } else {
          out_response->status_code = (uint16_t)code;
        }
        const char *transfer =
            find_header_value(response_headers, "Transfer-Encoding");
        if (status == HAL_OK && transfer != NULL &&
            strncasecmp(transfer, "chunked", 7u) == 0) {
          status = HAL_EUNSUPPORTED;
        }
        const char *length =
            find_header_value(response_headers, "Content-Length");
        if (status == HAL_OK && length != NULL) {
          size_t value = 0u;
          if (!parse_size_header_value(length, &value)) {
            status = HAL_EPROTO;
          } else {
            out_response->content_length_known = true;
            out_response->content_length = value;
          }
        }
      }
    }
    if (status == HAL_OK && headers_complete && offset < received) {
      const size_t body_part = received - offset;
      const size_t room = response_body_capacity - copied_body;
      const size_t copy = std::min(room, body_part);
      if (copy > 0u) {
        memcpy(static_cast<uint8_t *>(response_body) + copied_body,
               chunk + offset, copy);
      }
      copied_body += copy;
      total_body += body_part;
      if (out_response->content_length_known &&
          total_body >= out_response->content_length) {
        closed = true;
      }
    }
  }
  if (status == HAL_OK && !headers_complete) {
    status = closed ? HAL_EPROTO : HAL_ETIMEOUT;
  }
  if (status == HAL_OK && !closed) {
    status = HAL_ETIMEOUT;
  }
  if (status == HAL_OK && out_response->content_length_known &&
      total_body != out_response->content_length) {
    status = HAL_EPROTO;
  }
  out_response->body_length = total_body;
  (void)stream_close(&stream, request->timeout_ms);
  if (status == HAL_OK && total_body > response_body_capacity) {
    return HAL_EOVERFLOW;
  }
  return status;
}

#endif /* HAL_ENABLE_HTTP_CLIENT */

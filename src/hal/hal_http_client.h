#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_HTTP_CLIENT

#include "hal_status.h"
#include "hal_tls.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT = 0,
  HAL_HTTP_CLIENT_TRANSPORT_TLS = 1
} hal_http_client_transport_t;

typedef struct {
  const char *name;
  const char *value;
} hal_http_client_header_t;

typedef struct {
  hal_http_client_transport_t transport;
  const char *host;
  uint16_t port;
  const char *method;
  const char *path;
  const hal_http_client_header_t *headers;
  size_t header_count;
  const void *body;
  size_t body_length;
  uint32_t timeout_ms;
  const hal_tls_security_config_t *tls_security;
} hal_http_client_request_t;

typedef struct {
  uint16_t status_code;
  size_t body_length;
  size_t content_length;
  bool content_length_known;
} hal_http_client_response_t;

hal_status_t hal_http_client_request_init(hal_http_client_request_t *request);

/** Perform one bounded HTTP/1.1 request. The response body is copied to the
 * caller buffer without a terminator. HAL_EOVERFLOW reports a valid response
 * whose body did not fit; out_response still contains the required length. */
hal_status_t
hal_http_client_perform_ex(const hal_http_client_request_t *request,
                           void *response_body, size_t response_body_capacity,
                           hal_http_client_response_t *out_response);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_HTTP_CLIENT */

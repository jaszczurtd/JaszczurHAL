#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_HTTP_SERVER

#include "hal_net.h"
#include "hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_http_server.h
 * @brief Small poll-driven HTTP/1.1 server built on top of hal_tcp.
 *
 * The server intentionally keeps the public API independent of any concrete
 * TCP/IP stack. It accepts one request per connection, dispatches an exact
 * method/path route, sends a buffered response with Content-Length and closes
 * the connection.
 */

#ifndef HAL_HTTP_SERVER_MAX_ROUTES
#define HAL_HTTP_SERVER_MAX_ROUTES 8u
#endif

#ifndef HAL_HTTP_SERVER_MAX_CLIENTS
#define HAL_HTTP_SERVER_MAX_CLIENTS 2u
#endif

#ifndef HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE
#define HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE 512u
#endif

#ifndef HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE
#define HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE 1024u
#endif

#ifndef HAL_HTTP_SERVER_MAX_REQUEST_HEADERS
#define HAL_HTTP_SERVER_MAX_REQUEST_HEADERS 12u
#endif

#ifndef HAL_HTTP_SERVER_MAX_RESPONSE_HEADERS
#define HAL_HTTP_SERVER_MAX_RESPONSE_HEADERS 8u
#endif

#ifndef HAL_HTTP_SERVER_RESPONSE_HEADER_SIZE
#define HAL_HTTP_SERVER_RESPONSE_HEADER_SIZE 512u
#endif

#ifndef HAL_HTTP_SERVER_RESPONSE_HEADER_NAME_MAX
#define HAL_HTTP_SERVER_RESPONSE_HEADER_NAME_MAX 32u
#endif

#ifndef HAL_HTTP_SERVER_RESPONSE_HEADER_VALUE_MAX
#define HAL_HTTP_SERVER_RESPONSE_HEADER_VALUE_MAX 128u
#endif

#ifndef HAL_HTTP_SERVER_DEFAULT_BACKLOG
#define HAL_HTTP_SERVER_DEFAULT_BACKLOG 2u
#endif

#ifndef HAL_HTTP_SERVER_ROUTE_PATH_MAX
#define HAL_HTTP_SERVER_ROUTE_PATH_MAX 128u
#endif

#ifndef HAL_HTTP_SERVER_FIRST_BYTE_TIMEOUT_MS
#define HAL_HTTP_SERVER_FIRST_BYTE_TIMEOUT_MS 5000u
#endif

#ifndef HAL_HTTP_SERVER_REQUEST_TIMEOUT_MS
#define HAL_HTTP_SERVER_REQUEST_TIMEOUT_MS 15000u
#endif

#ifndef HAL_HTTP_SERVER_IDLE_TIMEOUT_MS
#define HAL_HTTP_SERVER_IDLE_TIMEOUT_MS 5000u
#endif

#if HAL_HTTP_SERVER_ROUTE_PATH_MAX < 2
#error "HAL_HTTP_SERVER_ROUTE_PATH_MAX must be at least 2"
#endif

typedef enum {
  HAL_HTTP_METHOD_UNKNOWN = 0,
  HAL_HTTP_METHOD_GET,
  HAL_HTTP_METHOD_HEAD,
  HAL_HTTP_METHOD_POST,
  HAL_HTTP_METHOD_PUT,
  HAL_HTTP_METHOD_DELETE,
  HAL_HTTP_METHOD_OPTIONS
} hal_http_method_t;

typedef struct {
  const char *name;
  const char *value;
} hal_http_header_t;

typedef struct {
  hal_http_method_t method;
  const char *path;
  const char *query;
  const char *body;
  size_t body_len;
  const hal_http_header_t *headers;
  size_t header_count;
  hal_net_endpoint_t remote;
} hal_http_request_t;

typedef struct hal_http_response_t hal_http_response_t;

typedef hal_status_t (*hal_http_handler_t)(const hal_http_request_t *request,
                                           hal_http_response_t *response,
                                           void *user);

/**
 * @brief Register an exact method/path route.
 *
 * The path is copied by the server and may be released after this call.
 */
hal_status_t hal_http_server_route(hal_http_method_t method, const char *path,
                                   hal_http_handler_t handler, void *user);

/**
 * @brief Register a prefix route. Exact routes are matched before prefixes.
 *
 * The path is copied by the server and may be released after this call.
 */
hal_status_t hal_http_server_route_prefix(hal_http_method_t method,
                                          const char *path_prefix,
                                          hal_http_handler_t handler,
                                          void *user);

/** @brief Remove all registered routes. Safe while the server is stopped. */
void hal_http_server_clear_routes(void);

/** @brief Start listening on all IPv4 interfaces at @p port. */
hal_status_t hal_http_server_start(uint16_t port);

/** @brief Stop the listener and close active client sockets. */
void hal_http_server_stop(void);

/** @brief Return true when the listener is active. */
bool hal_http_server_is_running(void);

/**
 * @brief Service accepts and active clients.
 *
 * Call regularly from the application loop. The function is non-blocking and
 * handles at most a small amount of work per call.
 */
void hal_http_server_poll(void);

/** @brief Set HTTP response status code and reason phrase. */
hal_status_t hal_http_response_set_status(hal_http_response_t *response,
                                          uint16_t status_code,
                                          const char *reason);

/** @brief Set Content-Type header. */
hal_status_t hal_http_response_set_content_type(hal_http_response_t *response,
                                                const char *content_type);

/** @brief Set or replace an arbitrary response header. */
hal_status_t hal_http_response_set_header(hal_http_response_t *response,
                                          const char *name, const char *value);

/** @brief Append raw response body bytes. */
hal_status_t hal_http_response_write(hal_http_response_t *response,
                                     const void *data, size_t len);

/** @brief Append a null-terminated response body string. */
hal_status_t hal_http_response_write_str(hal_http_response_t *response,
                                         const char *text);

/** @brief Return a request header value by case-insensitive name, or NULL. */
const char *hal_http_request_get_header(const hal_http_request_t *request,
                                        const char *name);

/** @brief Convert an HTTP method enum to a stable uppercase name. */
const char *hal_http_method_to_string(hal_http_method_t method);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_HTTP_SERVER */

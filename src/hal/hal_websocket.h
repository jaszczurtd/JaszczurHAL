#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_WEBSOCKET

#include "hal_net.h"
#include "hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_websocket.h
 * @brief Small poll-driven WebSocket server built on top of hal_tcp.
 *
 * The server performs the HTTP Upgrade handshake internally, then parses
 * single-frame text/binary messages from masked clients. It is intended for
 * embedded telemetry/control channels rather than as a full RFC 6455 stack.
 */

#ifndef HAL_WEBSOCKET_MAX_CLIENTS
#define HAL_WEBSOCKET_MAX_CLIENTS 2u
#endif

#ifndef HAL_WEBSOCKET_REQUEST_BUFFER_SIZE
#define HAL_WEBSOCKET_REQUEST_BUFFER_SIZE 512u
#endif

#ifndef HAL_WEBSOCKET_MAX_PAYLOAD_SIZE
#define HAL_WEBSOCKET_MAX_PAYLOAD_SIZE 256u
#endif

#ifndef HAL_WEBSOCKET_FRAME_BUFFER_SIZE
#define HAL_WEBSOCKET_FRAME_BUFFER_SIZE (HAL_WEBSOCKET_MAX_PAYLOAD_SIZE + 8u)
#endif

#ifndef HAL_WEBSOCKET_DEFAULT_BACKLOG
#define HAL_WEBSOCKET_DEFAULT_BACKLOG 2u
#endif

#ifndef HAL_WEBSOCKET_PATH_MAX
#define HAL_WEBSOCKET_PATH_MAX 128u
#endif

#ifndef HAL_WEBSOCKET_FIRST_BYTE_TIMEOUT_MS
#define HAL_WEBSOCKET_FIRST_BYTE_TIMEOUT_MS 5000u
#endif

#ifndef HAL_WEBSOCKET_HANDSHAKE_TIMEOUT_MS
#define HAL_WEBSOCKET_HANDSHAKE_TIMEOUT_MS 15000u
#endif

#ifndef HAL_WEBSOCKET_HANDSHAKE_IDLE_TIMEOUT_MS
#define HAL_WEBSOCKET_HANDSHAKE_IDLE_TIMEOUT_MS 5000u
#endif

#if HAL_WEBSOCKET_MAX_PAYLOAD_SIZE < 1
#error "HAL_WEBSOCKET_MAX_PAYLOAD_SIZE must be at least 1"
#endif

#if HAL_WEBSOCKET_FRAME_BUFFER_SIZE < (HAL_WEBSOCKET_MAX_PAYLOAD_SIZE + 8u)
#error "HAL_WEBSOCKET_FRAME_BUFFER_SIZE must hold payload and frame overhead"
#endif

#if HAL_WEBSOCKET_PATH_MAX < 2
#error "HAL_WEBSOCKET_PATH_MAX must be at least 2"
#endif

#define HAL_WEBSOCKET_INVALID_CLIENT 0xffu

typedef uint8_t hal_websocket_client_t;

typedef enum {
  HAL_WEBSOCKET_MESSAGE_TEXT = 1,
  HAL_WEBSOCKET_MESSAGE_BINARY = 2
} hal_websocket_message_type_t;

typedef struct {
  void (*on_connect)(hal_websocket_client_t client, void *user);
  void (*on_message)(hal_websocket_client_t client,
                     hal_websocket_message_type_t type, const uint8_t *data,
                     size_t len, void *user);
  void (*on_disconnect)(hal_websocket_client_t client, uint16_t close_code,
                        void *user);
} hal_websocket_callbacks_t;

hal_status_t
hal_websocket_server_set_callbacks(const hal_websocket_callbacks_t *callbacks,
                                   void *user);

/**
 * @brief Start the server and copy the accepted request path.
 *
 * The caller may release or modify @p path after this function returns.
 */
hal_status_t hal_websocket_server_start(uint16_t port, const char *path);
void hal_websocket_server_stop(void);
bool hal_websocket_server_is_running(void);
void hal_websocket_server_poll(void);

size_t hal_websocket_client_count(void);
bool hal_websocket_client_is_connected(hal_websocket_client_t client);

hal_status_t hal_websocket_send(hal_websocket_client_t client,
                                hal_websocket_message_type_t type,
                                const void *data, size_t len);
hal_status_t hal_websocket_send_text(hal_websocket_client_t client,
                                     const char *text);
hal_status_t hal_websocket_broadcast(hal_websocket_message_type_t type,
                                     const void *data, size_t len,
                                     size_t *sent_count);
hal_status_t hal_websocket_broadcast_text(const char *text, size_t *sent_count);

hal_status_t hal_websocket_close(hal_websocket_client_t client,
                                 uint16_t close_code);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_WEBSOCKET */

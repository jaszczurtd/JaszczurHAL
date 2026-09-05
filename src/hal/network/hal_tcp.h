#pragma once

#include "hal/core/hal_config.h"
#include "hal/network/hal_net.h"

#ifdef HAL_ENABLE_TCP

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_tcp.h
 * @brief TCP transport API.
 *
 * This module is opt-in and is compiled only when HAL_ENABLE_TCP is defined.
 * The API exposes opaque socket/listener handles and plain HAL network value
 * types; backend TCP/IP stack objects stay private to implementation files.
 * On RP CYW43 backends, status-returning operations report HAL_EUNSUPPORTED,
 * HAL_EUNINIT or HAL_EHW when the required board hardware is respectively
 * absent, inactive or known to have failed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Opaque TCP client socket handle. */
typedef struct hal_tcp_socket_impl_t *hal_tcp_socket_t;

/** @brief Opaque TCP listener/server handle. */
typedef struct hal_tcp_listener_impl_t *hal_tcp_listener_t;

hal_status_t hal_tcp_socket_open_ex(hal_tcp_socket_t *out_socket);
hal_status_t hal_tcp_socket_connect_ex(hal_tcp_socket_t socket,
                                       const hal_net_endpoint_t *remote,
                                       uint32_t timeout_ms);
/**
 * @brief Attempt to enqueue bytes on a connected TCP socket.
 * @param socket Connected socket handle; must not be NULL.
 * @param data Bytes to enqueue; may be NULL only when @p len is zero.
 * @param len Number of bytes offered; a successful write may be partial.
 * @param out_sent Required accepted-byte count, initialized to zero. Bytes
 *                 accepted before a later output error are still reported.
 * @return HAL_OK on success, HAL_EAGAIN when send capacity is temporarily
 *         exhausted, or another HAL error. Resume after the accepted bytes;
 *         readiness can be checked with hal_tcp_socket_can_send().
 */
hal_status_t hal_tcp_socket_send_ex(hal_tcp_socket_t socket, const void *data,
                                    size_t len, size_t *out_sent);
hal_status_t hal_tcp_socket_recv_ex(hal_tcp_socket_t socket, void *buffer,
                                    size_t max_len, uint32_t timeout_ms,
                                    size_t *out_received);
hal_status_t hal_tcp_listener_bind_ex(hal_tcp_listener_t listener,
                                      const hal_net_endpoint_t *local);
hal_status_t hal_tcp_listener_listen_ex(hal_tcp_listener_t listener,
                                        uint8_t backlog);
hal_status_t hal_tcp_listener_accept_ex(hal_tcp_listener_t listener,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        hal_tcp_socket_t *out_socket);
hal_status_t hal_tcp_listener_open_ex(hal_tcp_listener_t *out_listener);

/**
 * @brief Allocate a TCP client socket from the backend pool.
 * @return Socket handle, or NULL when no socket slot is available.
 */
hal_tcp_socket_t hal_tcp_socket_open(void);

/**
 * @brief Connect a TCP client socket to a remote family-tagged endpoint.
 * @param socket Socket handle returned by @ref hal_tcp_socket_open.
 * @param remote Remote endpoint. The port must be non-zero.
 * @param timeout_ms Connect timeout in milliseconds. Use
 *        @ref HAL_NET_TIMEOUT_FOREVER to wait without a fixed deadline.
 * @return true when the connection is established.
 */
bool hal_tcp_socket_connect(hal_tcp_socket_t socket,
                            const hal_net_endpoint_t *remote,
                            uint32_t timeout_ms);

/**
 * @brief Send bytes on a connected TCP client socket.
 * @param socket Connected TCP client socket.
 * @param data Payload bytes. May be NULL only when @p len is zero.
 * @param len Payload length in bytes.
 * @return Number of bytes accepted for transmission, or <0 on error.
 */
int hal_tcp_socket_send(hal_tcp_socket_t socket, const void *data, size_t len);

/**
 * @brief Receive bytes from a connected TCP client socket.
 * @param socket Connected TCP client socket.
 * @param buffer Destination buffer. May be NULL only when @p max_len is zero.
 * @param max_len Destination buffer size in bytes.
 * @param timeout_ms Timeout in milliseconds. Use 0 for a non-blocking poll and
 *        @ref HAL_NET_TIMEOUT_FOREVER to wait without a fixed deadline.
 * @return Number of bytes read, 0 when no bytes are available before the
 *         timeout or peer close, or <0 on error.
 */
int hal_tcp_socket_recv(hal_tcp_socket_t socket, void *buffer, size_t max_len,
                        uint32_t timeout_ms);

/**
 * @brief Check whether a TCP socket has bytes ready to read.
 *
 * This is a non-consuming readiness probe used by compatibility layers such as
 * BSD `select()`. It never blocks.
 * @param socket TCP socket handle.
 * @return true when a subsequent receive can complete immediately.
 */
bool hal_tcp_socket_can_recv(hal_tcp_socket_t socket);

/**
 * @brief Check whether a TCP socket can send immediately.
 *
 * This is a non-blocking readiness probe for compatibility layers.
 * @param socket TCP socket handle.
 * @return true when the socket is valid and connected.
 */
bool hal_tcp_socket_can_send(hal_tcp_socket_t socket);

/**
 * @brief Check whether a TCP client socket is currently connected.
 * @param socket Socket handle returned by @ref hal_tcp_socket_open.
 * @return true when the socket handle is valid and connected.
 */
bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket);

/**
 * @brief Stop I/O on a TCP client socket while keeping its handle allocated.
 * @param socket Socket handle. Passing NULL is ignored.
 */
void hal_tcp_socket_shutdown(hal_tcp_socket_t socket);

/**
 * @brief Close a TCP client socket and return its slot to the backend pool.
 * @param socket Socket handle. Passing NULL is ignored.
 */
void hal_tcp_socket_close(hal_tcp_socket_t socket);

/**
 * @brief Allocate a TCP listener from the backend pool.
 * @return Listener handle, or NULL when no listener slot is available.
 */
hal_tcp_listener_t hal_tcp_listener_open(void);

/**
 * @brief Bind a TCP listener to a local family-tagged endpoint.
 * @param listener Listener handle returned by @ref hal_tcp_listener_open.
 * @param local Local endpoint. The port must be non-zero.
 * @return true when the listener is bound.
 */
bool hal_tcp_listener_bind(hal_tcp_listener_t listener,
                           const hal_net_endpoint_t *local);

/**
 * @brief Start accepting TCP clients on a bound listener.
 * @param listener Bound TCP listener.
 * @param backlog Pending-connection depth requested by the caller. Backends
 *        may cap this to their static or platform limit.
 * @return true when the listener is active.
 */
bool hal_tcp_listener_listen(hal_tcp_listener_t listener, uint8_t backlog);

/**
 * @brief Accept the next pending TCP client.
 * @param listener Listening TCP listener.
 * @param remote Optional output endpoint for the peer.
 * @param timeout_ms Timeout in milliseconds. Use 0 for a non-blocking poll and
 *        @ref HAL_NET_TIMEOUT_FOREVER to wait without a fixed deadline.
 * @return Connected client socket, or NULL on timeout/no client/error.
 */
hal_tcp_socket_t hal_tcp_listener_accept(hal_tcp_listener_t listener,
                                         hal_net_endpoint_t *remote,
                                         uint32_t timeout_ms);

/**
 * @brief Check whether a TCP listener has a pending client ready to accept.
 *
 * This is a non-consuming readiness probe used by compatibility layers such as
 * BSD `select()`. It never blocks.
 * @param listener TCP listener handle.
 * @return true when a subsequent accept can complete immediately.
 */
bool hal_tcp_listener_can_accept(hal_tcp_listener_t listener);

/**
 * @brief Close a TCP listener and return its slot to the backend pool.
 *
 * Already accepted client sockets remain independent and are not closed.
 * @param listener Listener handle. Passing NULL is ignored.
 */
void hal_tcp_listener_close(hal_tcp_listener_t listener);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_TCP */

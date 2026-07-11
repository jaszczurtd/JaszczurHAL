#pragma once

#include "hal_config.h"
#include "hal_net.h"

#ifdef HAL_ENABLE_UDP

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_udp.h
 * @brief UDP transport API.
 *
 * This module is opt-in and is compiled only when HAL_ENABLE_UDP is defined.
 * New code should prefer the handle-based `hal_udp_socket_*` API, which can
 * model multiple independent UDP sockets. The older `hal_udp_*` functions are
 * kept as a source-compatible single-socket wrapper.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_UDP_IP_STR_LEN 16u

/** @brief Opaque UDP socket handle. */
typedef struct hal_udp_socket_impl_t *hal_udp_socket_t;

hal_status_t hal_udp_socket_bind_ex(hal_udp_socket_t socket,
                                    const hal_net_endpoint_t *local);
hal_status_t hal_udp_socket_sendto_ex(hal_udp_socket_t socket, const void *data,
                                      size_t len,
                                      const hal_net_endpoint_t *remote,
                                      size_t *out_sent);
hal_status_t hal_udp_socket_recvfrom_ex(hal_udp_socket_t socket, void *buffer,
                                        size_t max_len,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        size_t *out_received);
hal_status_t hal_udp_begin_ex(uint16_t local_port);
hal_status_t hal_udp_read_ex(uint8_t *buffer, uint16_t max_len,
                             uint16_t *out_read);
hal_status_t hal_udp_remote_ip_ex(char *out, size_t out_size);
hal_status_t hal_udp_begin_packet_ex(const char *host_or_ip,
                                     uint16_t remote_port);
hal_status_t hal_udp_begin_packet_remote_ex(void);
hal_status_t hal_udp_write_ex(const uint8_t *data, uint16_t len,
                              uint16_t *out_written);
hal_status_t hal_udp_write_str_ex(const char *text, uint16_t *out_written);
hal_status_t hal_udp_end_packet_ex(void);

/**
 * @brief Allocate a UDP socket from the backend pool.
 * @return Socket handle, or NULL when no socket slot is available.
 */
hal_udp_socket_t hal_udp_socket_open(void);

/**
 * @brief Bind a UDP socket to a local IPv4 endpoint.
 * @param socket Socket handle returned by @ref hal_udp_socket_open.
 * @param local Local IPv4 endpoint. The port must be non-zero.
 * @return true when the socket was bound.
 */
bool hal_udp_socket_bind(hal_udp_socket_t socket,
                         const hal_net_endpoint_t *local);

/**
 * @brief Send one UDP datagram to a remote IPv4 endpoint.
 * @param socket Bound UDP socket handle.
 * @param data Payload bytes. May be NULL only when @p len is zero.
 * @param len Payload length in bytes.
 * @param remote Remote IPv4 endpoint. The port must be non-zero.
 * @return Number of bytes accepted for transmission, or <0 on error.
 */
int hal_udp_socket_sendto(hal_udp_socket_t socket, const void *data, size_t len,
                          const hal_net_endpoint_t *remote);

/**
 * @brief Receive one UDP datagram from a bound socket.
 * @param socket Bound UDP socket handle.
 * @param buffer Destination buffer. May be NULL only when @p max_len is zero.
 * @param max_len Destination buffer size in bytes.
 * @param remote Optional output endpoint for the sender.
 * @param timeout_ms Timeout in milliseconds. Use 0 for a non-blocking poll and
 *        @ref HAL_NET_TIMEOUT_FOREVER to wait without a fixed deadline.
 * @return Number of bytes read, 0 when no datagram is available before the
 *         timeout, or <0 on error.
 */
int hal_udp_socket_recvfrom(hal_udp_socket_t socket, void *buffer,
                            size_t max_len, hal_net_endpoint_t *remote,
                            uint32_t timeout_ms);

/**
 * @brief Check whether a bound UDP socket has a datagram ready to read.
 *
 * This is a non-consuming readiness probe used by compatibility layers such as
 * BSD `select()`. It never blocks.
 * @param socket UDP socket handle.
 * @return true when a subsequent receive can complete immediately.
 */
bool hal_udp_socket_can_recv(hal_udp_socket_t socket);

/**
 * @brief Check whether a UDP socket can send immediately.
 *
 * This is a non-blocking readiness probe for compatibility layers. The socket
 * must be valid and bound for the HAL-level send API.
 * @param socket UDP socket handle.
 * @return true when a subsequent send can be attempted immediately.
 */
bool hal_udp_socket_can_send(hal_udp_socket_t socket);

/**
 * @brief Close a UDP socket and return its slot to the backend pool.
 * @param socket Socket handle. Passing NULL is ignored.
 */
void hal_udp_socket_close(hal_udp_socket_t socket);

/**
 * @brief Open UDP socket on local port.
 * @param local_port Local UDP port.
 * @return true on successful bind/open.
 */
bool hal_udp_begin(uint16_t local_port);

/** @brief Close UDP socket. */
void hal_udp_stop(void);

/**
 * @brief Parse next inbound datagram.
 * @return packet size in bytes, 0 when no packet is available, <0 on error.
 */
int hal_udp_parse_packet(void);

/**
 * @brief Read bytes from current datagram.
 * @param buffer Destination buffer.
 * @param max_len Maximum number of bytes to read.
 * @return Number of bytes read, 0 when nothing was read, <0 on error.
 */
int hal_udp_read(uint8_t *buffer, uint16_t max_len);

/**
 * @brief Write remote sender IP (from last parsed packet) to caller buffer.
 * @param out Destination buffer.
 * @param out_size Destination size in bytes.
 * @return true when remote sender is known and was written.
 */
bool hal_udp_remote_ip(char *out, size_t out_size);

/**
 * @brief Return remote sender port from last parsed packet.
 * @return 0 when remote sender is unknown.
 */
uint16_t hal_udp_remote_port(void);

/**
 * @brief Start outbound datagram to explicit host/IP and port.
 * @param host_or_ip Hostname or dotted IPv4 string.
 * @param remote_port Remote UDP port.
 * @return true when datagram context was started.
 */
bool hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port);

/**
 * @brief Start outbound datagram to remote sender from last parsed packet.
 * @return true when datagram context was started.
 */
bool hal_udp_begin_packet_remote(void);

/**
 * @brief Append raw payload bytes to current outbound datagram.
 * @param data Pointer to payload bytes.
 * @param len Number of bytes to append.
 * @return Number of bytes accepted.
 */
uint16_t hal_udp_write(const uint8_t *data, uint16_t len);

/**
 * @brief Append null-terminated text payload to current outbound datagram.
 * @param text Null-terminated string payload.
 * @return Number of bytes accepted.
 */
uint16_t hal_udp_write_str(const char *text);

/**
 * @brief Finalize and send outbound datagram.
 * @return true when datagram send succeeded.
 */
bool hal_udp_end_packet(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_UDP */

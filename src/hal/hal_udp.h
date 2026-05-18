#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_UDP

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_udp.h
 * @brief Thread-safe UDP helper wrapper based on WiFiUDP.
 *
 * This module is opt-in and is compiled only when HAL_ENABLE_UDP is defined.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_UDP_IP_STR_LEN 16u

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

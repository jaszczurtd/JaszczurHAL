#pragma once

#include "hal/hal_net.h"
#include "hal/hal_status.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define JH_NTP_PACKET_SIZE 48u
#ifndef HAL_TIME_NTP_PORT
#define HAL_TIME_NTP_PORT 123u
#endif
#define JH_NTP_PORT HAL_TIME_NTP_PORT

static inline void jh_ntp_write_u64_be(uint8_t *output, uint64_t value) {
  size_t index;
  for (index = 0u; index < 8u; ++index) {
    output[index] = (uint8_t)(value >> (56u - (index * 8u)));
  }
}

static inline uint32_t jh_ntp_read_u32_be(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
         ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

static inline hal_status_t
jh_ntp_prepare_request(uint8_t request[JH_NTP_PACKET_SIZE], uint64_t token) {
  if (request == NULL || token == 0u) {
    return HAL_EINVAL;
  }
  memset(request, 0, JH_NTP_PACKET_SIZE);
  request[0] = 0x23u; /* Leap=0, version=4, client mode. */
  jh_ntp_write_u64_be(&request[40], token);
  return HAL_OK;
}

static inline hal_status_t jh_ntp_validate_response(
    const uint8_t request[JH_NTP_PACKET_SIZE],
    const hal_net_endpoint_t *expected_server, const uint8_t *response,
    size_t response_size, const hal_net_endpoint_t *source,
    uint32_t *out_ntp_seconds, uint32_t *out_ntp_fraction) {
  uint8_t version;
  if (request == NULL || expected_server == NULL || response == NULL ||
      source == NULL || out_ntp_seconds == NULL || out_ntp_fraction == NULL) {
    return HAL_EINVAL;
  }
  *out_ntp_seconds = 0u;
  *out_ntp_fraction = 0u;

  if (expected_server->family != HAL_NET_AF_INET ||
      expected_server->addr_len != HAL_NET_IPV4_ADDR_LEN ||
      expected_server->scope_id != 0u || expected_server->port != JH_NTP_PORT ||
      source->family != HAL_NET_AF_INET ||
      source->addr_len != HAL_NET_IPV4_ADDR_LEN || source->scope_id != 0u ||
      source->port != JH_NTP_PORT ||
      memcmp(source->addr, expected_server->addr, HAL_NET_IPV4_ADDR_LEN) != 0) {
    return HAL_EPROTO;
  }
  if (response_size < JH_NTP_PACKET_SIZE) {
    return HAL_EPROTO;
  }

  version = (uint8_t)((response[0] >> 3u) & 0x07u);
  if ((response[0] >> 6u) == 3u || version < 3u || version > 4u ||
      (response[0] & 0x07u) != 4u || response[1] == 0u || response[1] > 15u ||
      memcmp(&response[24], &request[40], 8u) != 0) {
    return HAL_EPROTO;
  }

  *out_ntp_seconds = jh_ntp_read_u32_be(&response[40]);
  *out_ntp_fraction = jh_ntp_read_u32_be(&response[44]);
  if (*out_ntp_seconds == 0u && *out_ntp_fraction == 0u) {
    *out_ntp_seconds = 0u;
    *out_ntp_fraction = 0u;
    return HAL_EPROTO;
  }
  return HAL_OK;
}

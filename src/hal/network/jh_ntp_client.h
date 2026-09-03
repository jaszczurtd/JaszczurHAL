#pragma once

#include "hal/core/hal_status.h"
#include "hal/core/jh_endian.h"
#include "hal/network/hal_net.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define JH_NTP_PACKET_SIZE 48u
#ifndef HAL_TIME_NTP_PORT
#define HAL_TIME_NTP_PORT 123u
#endif
#define JH_NTP_PORT HAL_TIME_NTP_PORT

static inline hal_status_t
jh_ntp_prepare_request(uint8_t request[JH_NTP_PACKET_SIZE], uint64_t token) {
  if (request == NULL || token == 0u) {
    return HAL_EINVAL;
  }
  memset(request, 0, JH_NTP_PACKET_SIZE);
  request[0] = 0x23u; /* Leap=0, version=4, client mode. */
  jh_store_be64(&request[40], token);
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

  *out_ntp_seconds = jh_load_be32(&response[40]);
  *out_ntp_fraction = jh_load_be32(&response[44]);
  if (*out_ntp_seconds == 0u && *out_ntp_fraction == 0u) {
    *out_ntp_seconds = 0u;
    *out_ntp_fraction = 0u;
    return HAL_EPROTO;
  }
  return HAL_OK;
}

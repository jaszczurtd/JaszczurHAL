#pragma once

#include "hal/core/hal_status.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline hal_status_t
jh_icmp_echo_reply_parse(const uint8_t *packet, size_t packet_size,
                         uint16_t identifier, uint16_t sequence, int *out_ttl) {
  if (packet == NULL || out_ttl == NULL) {
    return HAL_EINVAL;
  }
  if (packet_size < 20u || (packet[0] >> 4u) != 4u) {
    return HAL_EPROTO;
  }
  const size_t ip_header_size = (size_t)(packet[0] & 0x0fu) * 4u;
  if (ip_header_size < 20u || packet_size < ip_header_size + 8u ||
      packet[9] != 1u) {
    return HAL_EPROTO;
  }
  const uint8_t *icmp = packet + ip_header_size;
  const uint16_t reply_identifier =
      (uint16_t)(((uint16_t)icmp[4] << 8u) | icmp[5]);
  const uint16_t reply_sequence =
      (uint16_t)(((uint16_t)icmp[6] << 8u) | icmp[7]);
  if (icmp[0] != 0u || icmp[1] != 0u || reply_identifier != identifier ||
      reply_sequence != sequence) {
    return HAL_ENOENT;
  }
  *out_ttl = (int)packet[8];
  return HAL_OK;
}

#ifdef __cplusplus
}
#endif

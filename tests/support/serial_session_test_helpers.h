#ifndef JH_SERIAL_SESSION_TEST_HELPERS_H
#define JH_SERIAL_SESSION_TEST_HELPERS_H

#include "hal/impl/.mock/hal_mock.h"
#include "hal/serial/hal_serial_frame.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

static bool build_framed_line(uint16_t seq, const char *payload, char *out,
                              size_t out_size, char terminator) {
  size_t frame_len = 0u;
  if (!hal_serial_frame_encode(seq, payload, out, out_size, &frame_len)) {
    return false;
  }
  if (frame_len + 2u > out_size) {
    return false;
  }
  out[frame_len] = terminator;
  out[frame_len + 1u] = '\0';
  return true;
}

static void inject_framed_line(uint16_t seq, const char *payload,
                               char terminator) {
  char line[HAL_SERIAL_FRAME_LINE_MAX + 2u];
  TEST_ASSERT_TRUE(
      build_framed_line(seq, payload, line, sizeof(line), terminator));
  hal_mock_serial_inject_rx(line, -1);
}

static bool decode_last_framed_reply(uint16_t *seq_out, char *payload_out,
                                     size_t payload_out_size) {
  return hal_serial_frame_decode(hal_mock_serial_last_line(), seq_out,
                                 payload_out, payload_out_size);
}

static void bytes_to_hex_lower(const uint8_t *bytes, size_t len, char *out,
                               size_t out_size) {
  static const char k_hex[] = "0123456789abcdef";
  if (len * 2u + 1u > out_size) {
    if (out_size > 0u) {
      out[0] = '\0';
    }
    return;
  }
  for (size_t i = 0u; i < len; ++i) {
    out[i * 2u] = k_hex[(bytes[i] >> 4) & 0x0Fu];
    out[i * 2u + 1u] = k_hex[bytes[i] & 0x0Fu];
  }
  out[len * 2u] = '\0';
}

static void hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
  for (size_t i = 0u; i < out_len; ++i) {
    unsigned int value = 0u;
    (void)sscanf(hex + i * 2u, "%2x", &value);
    out[i] = (uint8_t)value;
  }
}

#endif

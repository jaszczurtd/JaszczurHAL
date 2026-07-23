#include "jh_ota_protocol.h"

#include <limits.h>
#include <string.h>

namespace {

bool is_space(uint8_t value) {
  return value == static_cast<uint8_t>(' ') ||
         value == static_cast<uint8_t>('\t');
}

bool is_line_end(uint8_t value) {
  return value == static_cast<uint8_t>('\r') ||
         value == static_cast<uint8_t>('\n');
}

void skip_space(const uint8_t *data, size_t size, size_t *cursor) {
  while (*cursor < size && is_space(data[*cursor])) {
    ++(*cursor);
  }
}

bool parse_uint(const uint8_t *data, size_t size, size_t *cursor,
                uint32_t maximum, uint32_t *out_value) {
  skip_space(data, size, cursor);
  if (*cursor >= size || data[*cursor] < static_cast<uint8_t>('0') ||
      data[*cursor] > static_cast<uint8_t>('9')) {
    return false;
  }

  uint32_t value = 0u;
  while (*cursor < size && data[*cursor] >= static_cast<uint8_t>('0') &&
         data[*cursor] <= static_cast<uint8_t>('9')) {
    const uint32_t digit =
        static_cast<uint32_t>(data[*cursor] - static_cast<uint8_t>('0'));
    if (value > (maximum - digit) / 10u) {
      return false;
    }
    value = value * 10u + digit;
    ++(*cursor);
  }
  *out_value = value;
  return true;
}

bool is_hex(uint8_t value) {
  return (value >= static_cast<uint8_t>('0') &&
          value <= static_cast<uint8_t>('9')) ||
         (value >= static_cast<uint8_t>('a') &&
          value <= static_cast<uint8_t>('f')) ||
         (value >= static_cast<uint8_t>('A') &&
          value <= static_cast<uint8_t>('F'));
}

bool parse_hex_token(const uint8_t *data, size_t size, size_t *cursor,
                     char out[JH_OTA_MD5_HEX_BUFFER_SIZE]) {
  skip_space(data, size, cursor);
  for (size_t index = 0u; index < JH_OTA_MD5_HEX_CHARS; ++index) {
    if (*cursor >= size || !is_hex(data[*cursor])) {
      return false;
    }
    uint8_t value = data[*cursor];
    if (value >= static_cast<uint8_t>('A') &&
        value <= static_cast<uint8_t>('F')) {
      value = static_cast<uint8_t>(value - static_cast<uint8_t>('A') +
                                   static_cast<uint8_t>('a'));
    }
    out[index] = static_cast<char>(value);
    ++(*cursor);
  }
  out[JH_OTA_MD5_HEX_CHARS] = '\0';
  return *cursor >= size || is_space(data[*cursor]) ||
         is_line_end(data[*cursor]);
}

bool only_trailing_space(const uint8_t *data, size_t size, size_t cursor) {
  while (cursor < size) {
    if (!is_space(data[cursor]) && !is_line_end(data[cursor]) &&
        data[cursor] != 0u) {
      return false;
    }
    ++cursor;
  }
  return true;
}

} // namespace

extern "C" hal_status_t
jh_ota_parse_invitation(const uint8_t *data, size_t size,
                        jh_ota_invitation_t *out_invitation) {
  if (data == nullptr || size == 0u || out_invitation == nullptr) {
    return HAL_EINVAL;
  }

  jh_ota_invitation_t parsed{};
  size_t cursor = 0u;
  uint32_t command = 0u;
  uint32_t port = 0u;
  if (!parse_uint(data, size, &cursor, UINT16_MAX, &command) ||
      (command != 0u && command != 100u) ||
      !parse_uint(data, size, &cursor, UINT16_MAX, &port) || port == 0u ||
      !parse_uint(data, size, &cursor, UINT32_MAX, &parsed.image_size) ||
      parsed.image_size == 0u ||
      !parse_hex_token(data, size, &cursor, parsed.image_md5) ||
      !only_trailing_space(data, size, cursor)) {
    return HAL_EINVAL;
  }

  parsed.command = static_cast<uint16_t>(command);
  parsed.tcp_port = static_cast<uint16_t>(port);
  *out_invitation = parsed;
  return HAL_OK;
}

extern "C" hal_status_t
jh_ota_parse_auth_response(const uint8_t *data, size_t size,
                           jh_ota_auth_response_t *out_response) {
  if (data == nullptr || size == 0u || out_response == nullptr) {
    return HAL_EINVAL;
  }

  jh_ota_auth_response_t parsed{};
  size_t cursor = 0u;
  uint32_t command = 0u;
  if (!parse_uint(data, size, &cursor, UINT16_MAX, &command) ||
      command != 200u ||
      !parse_hex_token(data, size, &cursor, parsed.client_nonce) ||
      !parse_hex_token(data, size, &cursor, parsed.response) ||
      !only_trailing_space(data, size, cursor)) {
    return HAL_EINVAL;
  }

  *out_response = parsed;
  return HAL_OK;
}

extern "C" bool jh_ota_hex_equal(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  uint8_t difference = 0u;
  for (size_t index = 0u; index < JH_OTA_MD5_HEX_CHARS; ++index) {
    difference |=
        static_cast<uint8_t>(left[index]) ^ static_cast<uint8_t>(right[index]);
  }
  return difference == 0u && left[JH_OTA_MD5_HEX_CHARS] == '\0' &&
         right[JH_OTA_MD5_HEX_CHARS] == '\0';
}

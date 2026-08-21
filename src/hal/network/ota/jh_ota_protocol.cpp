#include "jh_ota_protocol.h"

#include <limits.h>
#include <stdio.h>
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

size_t bounded_string_length(const char *value, size_t capacity) {
  size_t length = 0u;
  while (length < capacity && value[length] != '\0') {
    ++length;
  }
  return length;
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
                     char *out, size_t hex_chars) {
  skip_space(data, size, cursor);
  for (size_t index = 0u; index < hex_chars; ++index) {
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
  out[hex_chars] = '\0';
  return *cursor >= size || is_space(data[*cursor]) ||
         is_line_end(data[*cursor]);
}

bool normalize_hex_token(const char *input, size_t hex_chars, char *out) {
  if (input == nullptr || out == nullptr ||
      bounded_string_length(input, hex_chars + 1u) != hex_chars) {
    return false;
  }
  for (size_t index = 0u; index < hex_chars; ++index) {
    uint8_t value = static_cast<uint8_t>(input[index]);
    if (!is_hex(value)) {
      return false;
    }
    if (value >= static_cast<uint8_t>('A') &&
        value <= static_cast<uint8_t>('F')) {
      value = static_cast<uint8_t>(value - static_cast<uint8_t>('A') +
                                   static_cast<uint8_t>('a'));
    }
    out[index] = static_cast<char>(value);
  }
  out[hex_chars] = '\0';
  return true;
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

bool hex_equal_fixed(const char *left, const char *right, size_t chars) {
  if (left == nullptr || right == nullptr ||
      bounded_string_length(left, chars + 1u) != chars ||
      bounded_string_length(right, chars + 1u) != chars) {
    return false;
  }
  uint8_t difference = 0u;
  for (size_t index = 0u; index < chars; ++index) {
    difference |=
        static_cast<uint8_t>(left[index]) ^ static_cast<uint8_t>(right[index]);
  }
  return difference == 0u;
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
      !parse_hex_token(data, size, &cursor, parsed.image_md5,
                       JH_OTA_MD5_HEX_CHARS) ||
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
      command != 201u ||
      !parse_hex_token(data, size, &cursor, parsed.client_nonce,
                       JH_OTA_MD5_HEX_CHARS) ||
      !parse_hex_token(data, size, &cursor, parsed.response,
                       JH_OTA_AUTH_TAG_HEX_CHARS) ||
      !only_trailing_space(data, size, cursor)) {
    return HAL_EINVAL;
  }

  *out_response = parsed;
  return HAL_OK;
}

extern "C" hal_status_t jh_ota_format_auth_transcript(
    const jh_ota_invitation_t *invitation, const char *device_nonce,
    const char *client_nonce, char *out, size_t out_size, size_t *out_length) {
  if (out_length != nullptr) {
    *out_length = 0u;
  }
  if (out != nullptr && out_size > 0u) {
    out[0] = '\0';
  }
  if (invitation == nullptr || out == nullptr || out_size == 0u ||
      (invitation->command != 0u && invitation->command != 100u) ||
      invitation->tcp_port == 0u || invitation->image_size == 0u) {
    return HAL_EINVAL;
  }

  char image_md5[JH_OTA_MD5_HEX_BUFFER_SIZE]{};
  char normalized_device_nonce[JH_OTA_MD5_HEX_BUFFER_SIZE]{};
  char normalized_client_nonce[JH_OTA_MD5_HEX_BUFFER_SIZE]{};
  if (!normalize_hex_token(invitation->image_md5, JH_OTA_MD5_HEX_CHARS,
                           image_md5) ||
      !normalize_hex_token(device_nonce, JH_OTA_MD5_HEX_CHARS,
                           normalized_device_nonce) ||
      !normalize_hex_token(client_nonce, JH_OTA_MD5_HEX_CHARS,
                           normalized_client_nonce)) {
    return HAL_EINVAL;
  }
  const int length =
      snprintf(out, out_size, "JHOTA-AUTH-2:%u:%u:%lu:%s:%s:%s",
               static_cast<unsigned>(invitation->command),
               static_cast<unsigned>(invitation->tcp_port),
               static_cast<unsigned long>(invitation->image_size), image_md5,
               normalized_device_nonce, normalized_client_nonce);
  if (length <= 0 || static_cast<size_t>(length) >= out_size) {
    out[0] = '\0';
    return HAL_EOVERFLOW;
  }
  if (out_length != nullptr) {
    *out_length = static_cast<size_t>(length);
  }
  return HAL_OK;
}

extern "C" bool jh_ota_hex_equal(const char *left, const char *right) {
  return hex_equal_fixed(left, right, JH_OTA_MD5_HEX_CHARS);
}

extern "C" bool jh_ota_auth_tag_equal(const char *left, const char *right) {
  return hex_equal_fixed(left, right, JH_OTA_AUTH_TAG_HEX_CHARS);
}

extern "C" bool jh_ota_endpoint_equal(const hal_net_endpoint_t *left,
                                      const hal_net_endpoint_t *right) {
  return left != nullptr && right != nullptr && left->family == right->family &&
         left->addr_len == right->addr_len && left->port == right->port &&
         left->scope_id == right->scope_id && left->addr_len > 0u &&
         left->addr_len <= sizeof(left->addr) &&
         memcmp(left->addr, right->addr, left->addr_len) == 0;
}

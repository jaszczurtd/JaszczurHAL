#include "hal/core/hal_text.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

int hex_value(char value) {
  const unsigned char c = (unsigned char)value;
  if (c >= (unsigned char)'0' && c <= (unsigned char)'9') {
    return (int)(c - (unsigned char)'0');
  }
  if (c >= (unsigned char)'A' && c <= (unsigned char)'F') {
    return (int)(c - (unsigned char)'A') + 10;
  }
  if (c >= (unsigned char)'a' && c <= (unsigned char)'f') {
    return (int)(c - (unsigned char)'a') + 10;
  }
  return -1;
}

char transliterated_polish(unsigned char first, unsigned char second) {
  if (first == 0xC3u) {
    return second == 0x93u ? 'O' : second == 0xB3u ? 'o' : '\0';
  }
  if (first == 0xC4u) {
    switch (second) {
    case 0x84u:
      return 'A';
    case 0x85u:
      return 'a';
    case 0x86u:
      return 'C';
    case 0x87u:
      return 'c';
    case 0x98u:
      return 'E';
    case 0x99u:
      return 'e';
    default:
      return '\0';
    }
  }
  if (first == 0xC5u) {
    switch (second) {
    case 0x81u:
      return 'L';
    case 0x82u:
      return 'l';
    case 0x83u:
      return 'N';
    case 0x84u:
      return 'n';
    case 0x9Au:
      return 'S';
    case 0x9Bu:
      return 's';
    case 0xB9u:
    case 0xBBu:
      return 'Z';
    case 0xBAu:
    case 0xBCu:
      return 'z';
    default:
      return '\0';
    }
  }
  return '\0';
}

} // namespace

hal_status_t hal_text_format_mac_ex(const uint8_t mac[6], char *buffer,
                                    size_t buffer_size) {
  if (mac == nullptr || buffer == nullptr) {
    return HAL_EINVAL;
  }
  if (buffer_size < HAL_TEXT_MAC_STRING_SIZE) {
    return HAL_EOVERFLOW;
  }
  const int written =
      snprintf(buffer, buffer_size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
               mac[1], mac[2], mac[3], mac[4], mac[5]);
  return written == (int)(HAL_TEXT_MAC_STRING_SIZE - 1u) ? HAL_OK : HAL_EIO;
}

char *hal_text_format_binary_int(int value, char *buffer, size_t buffer_size) {
  if (buffer == nullptr || buffer_size == 0u) {
    return buffer;
  }

  unsigned int bits = value < 0         ? (unsigned int)(sizeof(int) * 8u)
                      : value <= 0xFF   ? 8u
                      : value <= 0xFFFF ? 16u
                                        : 32u;
  if ((size_t)bits >= buffer_size) {
    bits = (unsigned int)(buffer_size - 1u);
  }
  memset(buffer, 0, buffer_size);
  const unsigned int unsigned_value = (unsigned int)value;
  for (unsigned int i = 0u; i < bits; ++i) {
    const unsigned int shift = bits - i - 1u;
    buffer[i] = (unsigned_value & (1u << shift)) != 0u ? '1' : '0';
  }
  buffer[bits] = '\0';
  return buffer;
}

hal_status_t hal_text_concat_ex(char *destination, size_t destination_size,
                                const char *first, const char *second) {
  if (destination == nullptr || first == nullptr || second == nullptr ||
      destination_size == 0u) {
    return HAL_EINVAL;
  }
  const size_t first_length = strlen(first);
  const size_t second_length = strlen(second);
  if (first_length >= destination_size ||
      second_length >= destination_size - first_length) {
    return HAL_EOVERFLOW;
  }
  memcpy(destination, first, first_length + 1u);
  memcpy(destination + first_length, second, second_length + 1u);
  return HAL_OK;
}

bool hal_text_concat(char *destination, size_t destination_size,
                     const char *first, const char *second) {
  return hal_text_concat_ex(destination, destination_size, first, second) ==
         HAL_OK;
}

bool hal_text_is_printable(const char *text, size_t maximum_size) {
  if (text == nullptr || maximum_size == 0u || text[0] == '\0') {
    return false;
  }
  for (size_t i = 0u; i < maximum_size; ++i) {
    const unsigned char c = (unsigned char)text[i];
    if (c == '\0') {
      return true;
    }
    if (isprint(c) == 0 && isspace(c) == 0) {
      return false;
    }
  }
  return false;
}

hal_status_t hal_text_hex_pair_to_byte_ex(char high, char low,
                                          uint8_t *out_value) {
  if (out_value == nullptr) {
    return HAL_EINVAL;
  }
  const int upper = hex_value(high);
  const int lower = hex_value(low);
  if (upper < 0 || lower < 0) {
    return HAL_EINVAL;
  }
  *out_value = (uint8_t)((upper << 4) | lower);
  return HAL_OK;
}

uint8_t hal_text_hex_pair_to_byte(char high, char low) {
  const int upper = isdigit((unsigned char)high) != 0
                        ? high - '0'
                        : toupper((unsigned char)high) - 'A' + 10;
  const int lower = isdigit((unsigned char)low) != 0
                        ? low - '0'
                        : toupper((unsigned char)low) - 'A' + 10;
  return (uint8_t)((upper << 4) | lower);
}

hal_status_t hal_text_url_decode_ex(const char *source, char *destination,
                                    size_t destination_size,
                                    size_t *out_length) {
  if (out_length != nullptr) {
    *out_length = 0u;
  }
  if (source == nullptr || destination == nullptr || destination_size == 0u) {
    return HAL_EINVAL;
  }

  size_t written = 0u;
  while (*source != '\0') {
    char decoded = *source;
    size_t consumed = 1u;
    if (*source == '%' && source[1] != '\0' && source[2] != '\0') {
      uint8_t byte = 0u;
      if (hal_text_hex_pair_to_byte_ex(source[1], source[2], &byte) == HAL_OK) {
        decoded = (char)byte;
        consumed = 3u;
      }
    } else if (*source == '+') {
      decoded = ' ';
    }
    if (written + 1u >= destination_size) {
      destination[written] = '\0';
      if (out_length != nullptr) {
        *out_length = written;
      }
      return HAL_EOVERFLOW;
    }
    destination[written++] = decoded;
    source += consumed;
  }
  destination[written] = '\0';
  if (out_length != nullptr) {
    *out_length = written;
  }
  return HAL_OK;
}

void hal_text_url_decode(const char *source, char *destination) {
  if (source == nullptr || destination == nullptr) {
    return;
  }
  (void)hal_text_url_decode_ex(source, destination, strlen(source) + 1u,
                               nullptr);
}

void hal_text_remove_whitespace(char *text) {
  if (text == nullptr) {
    return;
  }
  char *source = text;
  char *destination = text;
  while (*source != '\0') {
    if (isspace((unsigned char)*source) == 0) {
      *destination++ = *source;
    }
    ++source;
  }
  *destination = '\0';
}

int hal_text_parse_number(const char **text) {
  if (text == nullptr || *text == nullptr) {
    return 0;
  }
  int value = 0;
  while (isdigit((unsigned char)**text) != 0) {
    value = value * 10 + (**text - '0');
    ++(*text);
  }
  return value;
}

bool hal_text_starts_with(const char *text, const char *prefix) {
  if (text == nullptr || prefix == nullptr) {
    return false;
  }
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

hal_status_t hal_text_transliterate_ascii_ex(const char *input, char *output,
                                             size_t output_size) {
  if (input == nullptr || output == nullptr || output_size == 0u) {
    return HAL_EINVAL;
  }

  size_t input_index = 0u;
  size_t output_index = 0u;
  while (input[input_index] != '\0') {
    const unsigned char first = (unsigned char)input[input_index];
    char output_char = '\0';
    size_t consumed = 1u;
    if (first < 0x80u) {
      output_char = (char)first;
    } else if (input[input_index + 1u] != '\0' &&
               (first == 0xC3u || first == 0xC4u || first == 0xC5u)) {
      output_char =
          transliterated_polish(first, (unsigned char)input[input_index + 1u]);
      consumed = 2u;
    }

    if (output_char != '\0') {
      if (output_index + 1u >= output_size) {
        output[output_index] = '\0';
        return HAL_EOVERFLOW;
      }
      output[output_index++] = output_char;
    }
    input_index += consumed;
  }
  output[output_index] = '\0';
  return HAL_OK;
}

void hal_text_transliterate_ascii(const char *input, char *output,
                                  size_t output_size) {
  (void)hal_text_transliterate_ascii_ex(input, output, output_size);
}

hal_status_t hal_text_pack_field_ex(uint8_t *buffer, size_t width,
                                    const char *text, uint8_t padding) {
  if (buffer == nullptr || text == nullptr || width == 0u) {
    return HAL_EINVAL;
  }
  size_t length = strlen(text);
  if (length > width) {
    length = width;
  }
  for (size_t index = 0u; index < length; ++index) {
    buffer[index] = (uint8_t)text[index];
  }
  memset(buffer + length, padding, width - length);
  return HAL_OK;
}

void hal_text_pack_field_pad(uint8_t *buf, const char *str, int width,
                             uint8_t pad) {
  if (width > 0) {
    (void)hal_text_pack_field_ex(buf, (size_t)width, str, pad);
  }
}

void hal_text_pack_field(uint8_t *buf, const char *str, int width) {
  hal_text_pack_field_pad(buf, str, width, 0u);
}

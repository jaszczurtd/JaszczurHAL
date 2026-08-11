#pragma once

#include "hal/network/hal_net.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline bool
jh_net_parse_ipv4_literal(const char *src,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!src || !out_addr) {
    return false;
  }

  const char *cursor = src;
  for (size_t octet = 0u; octet < HAL_NET_IPV4_ADDR_LEN; ++octet) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    unsigned value = 0u;
    while (*cursor >= '0' && *cursor <= '9') {
      value = (value * 10u) + (unsigned)(*cursor - '0');
      if (value > 255u) {
        return false;
      }
      ++cursor;
    }

    out_addr[octet] = (uint8_t)value;
    if (octet < HAL_NET_IPV4_ADDR_LEN - 1u) {
      if (*cursor != '.') {
        return false;
      }
      ++cursor;
    } else if (*cursor != '\0') {
      return false;
    }
  }
  return true;
}

static inline int jh_net_hex_value(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

static inline bool jh_net_parse_ipv6_groups(const char *begin, const char *end,
                                            uint16_t out_words[8],
                                            size_t *out_count) {
  size_t count = 0u;
  const char *cursor = begin;
  while (cursor < end) {
    if (count >= 8u) {
      return false;
    }

    const char *group_end = cursor;
    while (group_end < end && *group_end != ':') {
      ++group_end;
    }
    if (memchr(cursor, '.', (size_t)(group_end - cursor)) != NULL) {
      if (group_end != end || count > 6u || group_end == cursor ||
          (size_t)(group_end - cursor) >= 16u) {
        return false;
      }
      char ipv4_text[16] = {};
      memcpy(ipv4_text, cursor, (size_t)(group_end - cursor));
      uint8_t ipv4[HAL_NET_IPV4_ADDR_LEN] = {};
      if (!jh_net_parse_ipv4_literal(ipv4_text, ipv4)) {
        return false;
      }
      out_words[count++] = (uint16_t)(((uint16_t)ipv4[0] << 8u) | ipv4[1]);
      out_words[count++] = (uint16_t)(((uint16_t)ipv4[2] << 8u) | ipv4[3]);
      cursor = group_end;
      continue;
    }

    uint16_t word = 0u;
    size_t digits = 0u;
    while (cursor < end && *cursor != ':') {
      const int digit = jh_net_hex_value(*cursor);
      if (digit < 0 || digits >= 4u) {
        return false;
      }
      word = (uint16_t)((word << 4u) | (uint16_t)digit);
      ++digits;
      ++cursor;
    }
    if (digits == 0u) {
      return false;
    }
    out_words[count++] = word;

    if (cursor < end) {
      ++cursor;
      if (cursor == end) {
        return false;
      }
    }
  }
  *out_count = count;
  return true;
}

static inline bool
jh_net_parse_ipv6_literal(const char *src,
                          uint8_t out_addr[HAL_NET_IPV6_ADDR_LEN],
                          uint32_t *out_scope_id, bool allow_scope) {
  if (!src || !out_addr) {
    return false;
  }

  const char *end = src + strlen(src);
  const char *scope = strchr(src, '%');
  uint32_t scope_id = 0u;
  if (scope) {
    if (!allow_scope || scope == src || scope + 1 == end ||
        strchr(scope + 1, '%') != NULL) {
      return false;
    }
    const char *cursor = scope + 1;
    while (cursor < end) {
      if (*cursor < '0' || *cursor > '9') {
        return false;
      }
      const uint32_t digit = (uint32_t)(*cursor - '0');
      if (scope_id > (UINT32_MAX - digit) / 10u) {
        return false;
      }
      scope_id = scope_id * 10u + digit;
      ++cursor;
    }
    end = scope;
  }
  if (src == end) {
    return false;
  }

  const char *compression = NULL;
  for (const char *cursor = src; cursor + 1 < end; ++cursor) {
    if (cursor[0] == ':' && cursor[1] == ':') {
      if (compression != NULL) {
        return false;
      }
      compression = cursor;
      ++cursor;
    }
  }

  uint16_t left[8] = {};
  uint16_t right[8] = {};
  size_t left_count = 0u;
  size_t right_count = 0u;
  if (compression) {
    if (!jh_net_parse_ipv6_groups(src, compression, left, &left_count) ||
        !jh_net_parse_ipv6_groups(compression + 2, end, right, &right_count) ||
        left_count + right_count >= 8u) {
      return false;
    }
  } else {
    if (!jh_net_parse_ipv6_groups(src, end, left, &left_count) ||
        left_count != 8u) {
      return false;
    }
  }

  uint16_t words[8] = {};
  memcpy(words, left, left_count * sizeof(left[0]));
  if (right_count > 0u) {
    memcpy(words + (8u - right_count), right, right_count * sizeof(right[0]));
  }
  for (size_t index = 0u; index < 8u; ++index) {
    out_addr[index * 2u] = (uint8_t)(words[index] >> 8u);
    out_addr[index * 2u + 1u] = (uint8_t)(words[index] & 0xffu);
  }
  if (out_scope_id) {
    *out_scope_id = scope_id;
  }
  return true;
}

static inline bool jh_net_format_ipv6(const uint8_t addr[HAL_NET_IPV6_ADDR_LEN],
                                      char *out, size_t out_size) {
  if (!addr || !out || out_size == 0u) {
    return false;
  }

  uint16_t words[8] = {};
  for (size_t index = 0u; index < 8u; ++index) {
    words[index] =
        (uint16_t)(((uint16_t)addr[index * 2u] << 8u) | addr[index * 2u + 1u]);
  }

  size_t best_start = 8u;
  size_t best_length = 0u;
  for (size_t start = 0u; start < 8u;) {
    if (words[start] != 0u) {
      ++start;
      continue;
    }
    size_t end = start;
    while (end < 8u && words[end] == 0u) {
      ++end;
    }
    const size_t length = end - start;
    if (length >= 2u && length > best_length) {
      best_start = start;
      best_length = length;
    }
    start = end;
  }

  char text[46] = {};
  size_t used = 0u;
  for (size_t index = 0u; index < 8u;) {
    if (index == best_start) {
      if (used + 2u >= sizeof(text)) {
        return false;
      }
      text[used++] = ':';
      text[used++] = ':';
      index += best_length;
      continue;
    }
    if (used > 0u && text[used - 1u] != ':') {
      text[used++] = ':';
    }
    const int written = snprintf(text + used, sizeof(text) - used, "%x",
                                 (unsigned)words[index]);
    if (written < 0 || (size_t)written >= sizeof(text) - used) {
      return false;
    }
    used += (size_t)written;
    ++index;
  }

  if (used + 1u > out_size) {
    return false;
  }
  memcpy(out, text, used + 1u);
  return true;
}

static inline bool jh_net_address_is_unspecified(const uint8_t *addr,
                                                 size_t addr_len) {
  if (!addr) {
    return true;
  }
  for (size_t index = 0u; index < addr_len; ++index) {
    if (addr[index] != 0u) {
      return false;
    }
  }
  return true;
}

static inline hal_status_t
jh_net_validate_endpoint_shape(const hal_net_endpoint_t *endpoint,
                               bool require_port,
                               bool allow_unspecified_address) {
  if (!endpoint) {
    return HAL_EINVAL;
  }
  if (endpoint->family == HAL_NET_AF_INET) {
    if (endpoint->addr_len != HAL_NET_IPV4_ADDR_LEN ||
        endpoint->scope_id != 0u) {
      return HAL_EINVAL;
    }
    for (size_t index = HAL_NET_IPV4_ADDR_LEN; index < HAL_NET_MAX_ADDR_LEN;
         ++index) {
      if (endpoint->addr[index] != 0u) {
        return HAL_EINVAL;
      }
    }
  } else if (endpoint->family == HAL_NET_AF_INET6) {
    if (endpoint->addr_len != HAL_NET_IPV6_ADDR_LEN) {
      return HAL_EINVAL;
    }
  } else {
    return HAL_EINVAL;
  }

  if (require_port && endpoint->port == 0u) {
    return HAL_EINVAL;
  }
  if (!allow_unspecified_address &&
      jh_net_address_is_unspecified(endpoint->addr, endpoint->addr_len)) {
    return HAL_EINVAL;
  }
  return HAL_OK;
}

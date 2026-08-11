#ifndef JH_HTTP_TEXT_H
#define JH_HTTP_TEXT_H

#include "hal/network/hal_tcp.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline char jh_http_lower_ascii(char value) {
  return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static inline bool jh_http_str_ieq(const char *left, const char *right) {
  if (left == NULL || right == NULL) {
    return false;
  }
  while (*left != '\0' && *right != '\0') {
    if (jh_http_lower_ascii(*left++) != jh_http_lower_ascii(*right++)) {
      return false;
    }
  }
  return *left == '\0' && *right == '\0';
}

static inline bool jh_http_str_nieq(const char *left, const char *right,
                                    size_t len) {
  if (left == NULL || right == NULL) {
    return false;
  }
  for (size_t i = 0u; i < len; ++i) {
    if (left[i] == '\0' || right[i] == '\0' ||
        jh_http_lower_ascii(left[i]) != jh_http_lower_ascii(right[i])) {
      return false;
    }
  }
  return true;
}

static inline bool jh_http_copy_string(char *dst, size_t dst_size,
                                       const char *src) {
  if (dst == NULL || dst_size == 0u || src == NULL) {
    return false;
  }
  const size_t len = strlen(src);
  if (len >= dst_size) {
    return false;
  }
  memcpy(dst, src, len + 1u);
  return true;
}

static inline bool jh_http_append_string(char *dst, size_t dst_size,
                                         const char *src) {
  const size_t used = strlen(dst);
  const size_t len = strlen(src);
  if (used + len >= dst_size) {
    return false;
  }
  memcpy(dst + used, src, len + 1u);
  return true;
}

static inline bool jh_http_append_text(char *out, size_t out_size, size_t *pos,
                                       const char *text) {
  const size_t len = strlen(text);
  if (*pos + len >= out_size) {
    return false;
  }
  memcpy(out + *pos, text, len);
  *pos += len;
  out[*pos] = '\0';
  return true;
}

#ifdef HAL_ENABLE_TCP
static inline bool jh_http_send_all(hal_tcp_socket_t socket, const void *data,
                                    size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  size_t sent = 0u;
  while (sent < len) {
    const int result = hal_tcp_socket_send(socket, bytes + sent, len - sent);
    if (result <= 0) {
      return false;
    }
    sent += (size_t)result;
  }
  return true;
}
#endif

#ifdef __cplusplus
template <typename Response>
static hal_status_t jh_buffered_response_write(Response *response,
                                               const void *data, size_t len) {
  if (response == nullptr || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (response->body_len + len >= sizeof(response->body)) {
    response->overflow = true;
    return HAL_EOVERFLOW;
  }
  if (len > 0u) {
    memcpy(response->body + response->body_len, data, len);
    response->body_len += len;
    response->body[response->body_len] = '\0';
  }
  return HAL_OK;
}
#endif

#endif

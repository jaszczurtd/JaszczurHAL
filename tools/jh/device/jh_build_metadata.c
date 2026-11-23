#include "jh_build_metadata.h"
#include <string.h>

bool jh_metadata_next_line(jb_json_span_t *input, jb_json_span_t *line) {
  if (input == NULL || line == NULL || input->length == 0)
    return false;
  const char *end = memchr(input->data, '\n', input->length);
  const size_t length = end ? (size_t)(end - input->data) : input->length;
  *line = (jb_json_span_t){input->data, length};
  const size_t consumed = length + (end != NULL ? 1u : 0u);
  input->data += consumed;
  input->length -= consumed;
  if (line->length && line->data[line->length - 1] == '\r')
    --line->length;
  return true;
}

hal_status_t jh_cmake_cache_find(jb_json_span_t cache, const char *key,
                                 jb_json_span_t *value, bool *found) {
  if (key == NULL || value == NULL || found == NULL ||
      (cache.data == NULL && cache.length != 0))
    return HAL_EINVAL;
  const size_t n = strlen(key);
  jb_json_span_t line;
  *value = jh_span("");
  *found = false;
  while (jh_metadata_next_line(&cache, &line)) {
    if (line.length <= n || memcmp(line.data, key, n) != 0 ||
        line.data[n] != ':')
      continue;
    const char *eq = memchr(line.data + n, '=', line.length - n);
    if (eq == NULL || *found)
      return HAL_ECONFIG;
    const jb_json_span_t text = {eq + 1,
                                 line.length - (size_t)(eq + 1 - line.data)};
    if (memchr(text.data, '\0', text.length) != NULL)
      return HAL_EPROTO;
    *value = text;
    *found = true;
  }
  return HAL_OK;
}

bool jh_metadata_unsigned(jb_json_span_t text, uint64_t *out) {
  unsigned base = 10;
  size_t i = 0;
  if (out == NULL || (text.data == NULL && text.length != 0))
    return false;
  if (text.length > 2 && text.data[0] == '0' &&
      (text.data[1] == 'x' || text.data[1] == 'X')) {
    base = 16;
    i = 2;
  }
  if (i == text.length)
    return false;
  uint64_t value = 0;
  for (; i < text.length; ++i) {
    const int digit = jh_hex_digit(text.data[i]);
    if (digit < 0 || (unsigned)digit >= base ||
        value > (UINT64_MAX - (unsigned)digit) / base)
      return false;
    value = value * base + (unsigned)digit;
  }
  *out = value;
  return true;
}

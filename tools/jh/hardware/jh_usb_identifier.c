#include "jh_usb_identifier.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <jb/utf8.h>
#include <string.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

typedef struct {
  pcre2_code *code;
  pcre2_match_data *match;
  hal_status_t status;
} decimal_t;

static bool decimal_scalar(decimal_t *decimal, uint32_t scalar) {
  if (decimal->status != HAL_OK)
    return false;
  if (decimal->code == NULL) {
    int error = 0;
    PCRE2_SIZE offset = 0;
    decimal->code =
        pcre2_compile((PCRE2_SPTR) "\\A\\p{Nd}\\z", PCRE2_ZERO_TERMINATED,
                      PCRE2_UTF | PCRE2_UCP, &error, &offset, NULL);
    if (decimal->code != NULL)
      decimal->match =
          pcre2_match_data_create_from_pattern(decimal->code, NULL);
    if (decimal->code == NULL || decimal->match == NULL) {
      decimal->status = HAL_ENOMEM;
      return false;
    }
  }
  char bytes[4];
  size_t length = 0;
  if (jb_utf8_encode(scalar, bytes, sizeof bytes, &length) != HAL_OK)
    return false;
  const int result = pcre2_match(decimal->code, (PCRE2_SPTR)bytes, length, 0, 0,
                                 decimal->match, NULL);
  if (result < 0 && result != PCRE2_ERROR_NOMATCH)
    decimal->status =
        result == PCRE2_ERROR_NOMEMORY ? HAL_ENOMEM : HAL_EINTERNAL;
  return result >= 0;
}

/* Unicode decimal digits occur in complete, contiguous sets of ten. The
 * mathematical styled digits have adjacent sets, hence modulo ten. */
static int digit(decimal_t *decimal, uint32_t scalar) {
  if (scalar < 128)
    return jh_hex_digit((char)scalar);
  if (!decimal_scalar(decimal, scalar))
    return -1;
  uint32_t first = scalar;
  while (first && decimal->status == HAL_OK &&
         decimal_scalar(decimal, first - 1))
    --first;
  return (int)((scalar - first) % 10);
}

static jh_usb_id_kind_t parse_text(decimal_t *decimal, jb_json_span_t text,
                                   uint32_t *out) {
  if (!jh_python_strip(text, &text))
    return JH_USB_ID_INVALID;
  if (!text.length)
    return JH_USB_ID_ABSENT;
  unsigned base = 10;
  size_t scalars = 0;
  for (size_t i = 0; i < text.length;) {
    uint32_t scalar = 0;
    size_t consumed = 0;
    if (jb_utf8_decode(text.data + i, text.length - i, &scalar, &consumed) !=
        HAL_OK)
      return JH_USB_ID_INVALID;
    ++scalars;
    if ((scalar >= 'a' && scalar <= 'f') || (scalar >= 'A' && scalar <= 'F'))
      base = 16;
    i += consumed;
  }
  if (scalars == 4 || (text.length >= 2 && text.data[0] == '0' &&
                       (text.data[1] == 'x' || text.data[1] == 'X')))
    base = 16;
  size_t at = 0;
  const bool negative = text.data[0] == '-';
  if (negative || text.data[0] == '+')
    ++at;
  if (base == 16 && text.length - at >= 2 && text.data[at] == '0' &&
      (text.data[at + 1] == 'x' || text.data[at + 1] == 'X')) {
    at += 2;
    if (at < text.length && text.data[at] == '_')
      ++at;
  }
  uint32_t total = 0;
  bool previous_digit = false;
  while (at < text.length) {
    if (text.data[at] == '_') {
      if (!previous_digit)
        return JH_USB_ID_INVALID;
      previous_digit = false;
      ++at;
      continue;
    }
    uint32_t scalar = 0;
    size_t consumed = 0;
    if (jb_utf8_decode(text.data + at, text.length - at, &scalar, &consumed) !=
        HAL_OK)
      return JH_USB_ID_INVALID;
    const int value_of = digit(decimal, scalar);
    if (value_of < 0 || (unsigned)value_of >= base)
      return JH_USB_ID_INVALID;
    if (total <= 65535)
      total = total * base + (unsigned)value_of;
    previous_digit = true;
    at += consumed;
  }
  if (!previous_digit)
    return JH_USB_ID_INVALID;
  if (total > 65535 || (negative && total))
    return JH_USB_ID_RANGE;
  *out = total;
  return JH_USB_ID_VALUE;
}

hal_status_t jh_usb_identifier(jb_json_tree_view_t value,
                               jh_usb_id_kind_t *kind, uint32_t *out) {
  if (kind == NULL || out == NULL)
    return HAL_EINVAL;
  *kind = JH_USB_ID_ABSENT;
  jb_json_span_t text = {0};
  int64_t number = 0;
  if (jh_json_int64(value, &number)) {
    if (number < 0 || number > 65535) {
      *kind = JH_USB_ID_RANGE;
    } else {
      *kind = JH_USB_ID_VALUE;
      *out = (uint32_t)number;
    }
    return HAL_OK;
  }
  if (jh_json_type(value) == JB_JSON_NUMBER) {
    (void)jb_json_tree_text(value, &text);
    /* Out-of-int64 integer tokens are still Python ints; floats are ignored. */
    if (memchr(text.data, '.', text.length) == NULL &&
        memchr(text.data, 'e', text.length) == NULL &&
        memchr(text.data, 'E', text.length) == NULL &&
        memchr(text.data, 'I', text.length) == NULL &&
        memchr(text.data, 'N', text.length) == NULL)
      *kind = JH_USB_ID_RANGE;
    return HAL_OK;
  }
  if (!jh_json_string(value, &text))
    return HAL_OK;
  decimal_t decimal = {NULL, NULL, HAL_OK};
  const jh_usb_id_kind_t parsed = parse_text(&decimal, text, out);
  pcre2_match_data_free(decimal.match);
  pcre2_code_free(decimal.code);
  if (decimal.status != HAL_OK)
    return decimal.status;
  *kind = parsed;
  return HAL_OK;
}

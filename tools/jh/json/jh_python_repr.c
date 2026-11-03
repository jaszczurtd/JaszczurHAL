#include "jh_python_repr.h"
#include <jb/unicode.h>
#include <jb/utf8.h>
#include <string.h>

/* A backslash, kind and digits hex digits of scalar: at most 10 bytes. */
typedef struct {
  char bytes[10];
  size_t length;
} escape_t;

static escape_t escaped(uint32_t scalar, char kind, unsigned digits) {
  static const char hex[] = "0123456789abcdef";
  escape_t out = {{'\\', kind}, 2};
  for (unsigned shift = digits * 4; shift != 0; shift -= 4)
    out.bytes[out.length++] = hex[(scalar >> (shift - 4)) & 15];
  return out;
}

static hal_status_t emit(jh_repr_piece_fn piece, void *user, const char *data,
                         size_t length, size_t consumed) {
  const jb_json_span_t text = {data, length};
  return piece(user, text, consumed);
}

static hal_status_t emit_escape(jh_repr_piece_fn piece, void *user,
                                escape_t value, size_t consumed) {
  return emit(piece, user, value.bytes, value.length, consumed);
}

/* The piece of one decoded scalar of used bytes at bytes. */
static hal_status_t emit_scalar(jh_repr_piece_fn piece, void *user,
                                const char *bytes, size_t used, uint32_t scalar,
                                char quote) {
  if (scalar == (uint32_t)(unsigned char)quote || scalar == '\\') {
    const char pair[] = {'\\', (char)scalar};
    return emit(piece, user, pair, 2, used);
  }
  if (scalar == '\t')
    return emit(piece, user, "\\t", 2, used);
  if (scalar == '\n')
    return emit(piece, user, "\\n", 2, used);
  if (scalar == '\r')
    return emit(piece, user, "\\r", 2, used);
  if (scalar < 0x20 || scalar == 0x7f)
    return emit_escape(piece, user, escaped(scalar, 'x', 2), used);
  if (scalar < 0x7f)
    return emit(piece, user, bytes, used, used);
  jb_unicode_properties_t properties;
  if (jb_unicode_properties(scalar, &properties) == HAL_OK &&
      properties.printable)
    return emit(piece, user, bytes, used, used);
  return emit_escape(piece, user,
                     scalar < 0x100     ? escaped(scalar, 'x', 2)
                     : scalar < 0x10000 ? escaped(scalar, 'u', 4)
                                        : escaped(scalar, 'U', 8),
                     used);
}

hal_status_t jh_python_repr(jb_json_span_t text, jh_repr_piece_fn piece,
                            void *user) {
  if (piece == NULL || (text.data == NULL && text.length != 0))
    return HAL_EINVAL;
  const bool single = text.length != 0 && memchr(text.data, '\'', text.length);
  const bool double_quote =
      text.length != 0 && memchr(text.data, '"', text.length);
  const char quote = single && !double_quote ? '"' : '\'';
  hal_status_t status = emit(piece, user, &quote, 1, 0);
  for (size_t i = 0; status == HAL_OK && i < text.length;) {
    uint32_t scalar = 0;
    size_t used = 1;
    if (jb_utf8_decode(text.data + i, text.length - i, &scalar, &used) !=
        HAL_OK) {
      status = emit_escape(piece, user,
                           escaped((unsigned char)text.data[i], 'x', 2), 1);
      ++i;
      continue;
    }
    status = emit_scalar(piece, user, text.data + i, used, scalar, quote);
    i += used;
  }
  return status == HAL_OK ? emit(piece, user, &quote, 1, 0) : status;
}

static hal_status_t append_piece(void *user, jb_json_span_t piece,
                                 size_t consumed) {
  (void)consumed;
  return jb_string_append((jb_string_t *)user, piece.data, piece.length);
}

hal_status_t jh_python_repr_append(jb_string_t *out, jb_json_span_t text) {
  if (out == NULL)
    return HAL_EINVAL;
  // text may point into out: the quoted text is built aside, so growing out
  // cannot move the bytes still being read.
  jb_string_t quoted = {0};
  hal_status_t status = jb_string_init(&quoted, &out->allocator);
  if (status == HAL_OK)
    status = jh_python_repr(text, append_piece, &quoted);
  if (status == HAL_OK)
    status =
        jb_string_append(out, jb_string_data(&quoted), jb_string_size(&quoted));
  jb_string_destroy(&quoted);
  return status;
}

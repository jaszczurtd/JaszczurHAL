/** @file Shared serial/debug formatting implementation. */
#include "hal_debug_format.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  HAL_FMT_LEN_NONE = 0,
  HAL_FMT_LEN_HH,
  HAL_FMT_LEN_H,
  HAL_FMT_LEN_L,
  HAL_FMT_LEN_LL,
  HAL_FMT_LEN_J,
  HAL_FMT_LEN_Z,
  HAL_FMT_LEN_T,
  HAL_FMT_LEN_CAP_L,
} hal_fmt_length_t;

typedef struct {
  bool left;
  bool plus;
  bool space;
  bool alt;
  bool zero;
  bool width_specified;
  int width;
  bool precision_specified;
  int precision;
  hal_fmt_length_t length;
  char spec;
  char fmt[80];
} hal_fmt_spec_t;

typedef struct {
  hal_debug_format_write_fn write;
  void *ctx;
  size_t total;
} hal_fmt_writer_t;

typedef struct {
  va_list ap;
} hal_fmt_arg_cursor_t;

static void emit_bytes(hal_fmt_writer_t *w, const char *data, size_t len) {
  if (w == NULL || w->write == NULL || data == NULL || len == 0u) {
    return;
  }

  w->write(w->ctx, data, len);
  if (SIZE_MAX - w->total < len) {
    w->total = SIZE_MAX;
  } else {
    w->total += len;
  }
}

static void emit_cstr(hal_fmt_writer_t *w, const char *text) {
  if (text == NULL) {
    text = "";
  }
  emit_bytes(w, text, strlen(text));
}

void hal_debug_format_write_cstr(hal_debug_format_write_fn write, void *ctx,
                                 const char *text) {
  hal_fmt_writer_t w = {write, ctx, 0u};
  emit_cstr(&w, text);
}

void hal_debug_format_write_error_prefix(hal_debug_format_write_fn write,
                                         void *ctx, const char *timestamp) {
  hal_fmt_writer_t w = {write, ctx, 0u};
  if (timestamp != NULL && timestamp[0] != '\0') {
    emit_cstr(&w, "[");
    emit_cstr(&w, timestamp);
    emit_cstr(&w, "] ");
  }
  emit_cstr(&w, HAL_DEBUG_ERROR_PREFIX);
}

void hal_debug_format_write_deb_prefix(hal_debug_format_write_fn write,
                                       void *ctx, const char *prefix) {
  hal_fmt_writer_t w = {write, ctx, 0u};
  if (prefix != NULL && prefix[0] != '\0') {
    emit_cstr(&w, prefix);
    emit_cstr(&w, " ");
  }
}

void hal_debug_format_write_source_prefix(hal_debug_format_write_fn write,
                                          void *ctx, const char *source) {
  hal_fmt_writer_t w = {write, ctx, 0u};
  emit_cstr(&w, "[");
  emit_cstr(&w, (source != NULL && source[0] != '\0') ? source : "global");
  emit_cstr(&w, "] ");
}

void hal_debug_format_write_isr_prefix(hal_debug_format_write_fn write,
                                       void *ctx, bool error_level,
                                       const char *prefix,
                                       const char *timestamp_us) {
  hal_fmt_writer_t w = {write, ctx, 0u};
  const char *ts =
      (timestamp_us != NULL && timestamp_us[0] != '\0') ? timestamp_us : "0";

  if (error_level) {
    emit_cstr(&w, HAL_DEBUG_ERROR_PREFIX);
    emit_cstr(&w, "[ISR ts=");
  } else if (prefix != NULL && prefix[0] != '\0') {
    emit_cstr(&w, prefix);
    emit_cstr(&w, " [ISR ts=");
  } else {
    emit_cstr(&w, "[ISR ts=");
  }

  emit_cstr(&w, ts);
  emit_cstr(&w, "] ");
}

static void emit_repeat(hal_fmt_writer_t *w, char ch, size_t count) {
  char buf[16];
  memset(buf, ch, sizeof(buf));
  while (count > 0u) {
    size_t chunk = count < sizeof(buf) ? count : sizeof(buf);
    emit_bytes(w, buf, chunk);
    count -= chunk;
  }
}

static size_t bounded_strlen(const char *text, size_t max_len) {
  size_t n = 0u;
  while (n < max_len && text[n] != '\0') {
    ++n;
  }
  return n;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static void append_char(char *dst, size_t *pos, size_t cap, char ch) {
  if (*pos + 1u < cap) {
    dst[*pos] = ch;
  }
  ++(*pos);
}

static void append_uint(char *dst, size_t *pos, size_t cap, unsigned value) {
  char tmp[16];
  size_t n = 0u;

  do {
    tmp[n++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value != 0u && n < sizeof(tmp));

  while (n > 0u) {
    append_char(dst, pos, cap, tmp[--n]);
  }
}

static int parse_int_digits(const char **p) {
  int value = 0;
  while (is_digit(**p)) {
    int digit = **p - '0';
    if (value > (INT_MAX - digit) / 10) {
      value = INT_MAX;
    } else {
      value = value * 10 + digit;
    }
    ++(*p);
  }
  return value;
}

static void set_width_from_arg(hal_fmt_spec_t *spec, int width) {
  spec->width_specified = true;
  if (width < 0) {
    spec->left = true;
    spec->width = (width == INT_MIN) ? INT_MAX : -width;
  } else {
    spec->width = width;
  }
}

static const char *parse_spec(const char *p, hal_fmt_spec_t *spec,
                              hal_fmt_arg_cursor_t *args) {
  memset(spec, 0, sizeof(*spec));
  spec->length = HAL_FMT_LEN_NONE;

  bool done = false;
  while (!done) {
    switch (*p) {
    case '-':
      spec->left = true;
      ++p;
      break;
    case '+':
      spec->plus = true;
      ++p;
      break;
    case ' ':
      spec->space = true;
      ++p;
      break;
    case '#':
      spec->alt = true;
      ++p;
      break;
    case '0':
      spec->zero = true;
      ++p;
      break;
    default:
      done = true;
      break;
    }
  }

  if (*p == '*') {
    set_width_from_arg(spec, va_arg(args->ap, int));
    ++p;
  } else if (is_digit(*p)) {
    spec->width_specified = true;
    spec->width = parse_int_digits(&p);
  }

  if (*p == '.') {
    spec->precision_specified = true;
    ++p;
    if (*p == '*') {
      int precision = va_arg(args->ap, int);
      if (precision < 0) {
        spec->precision_specified = false;
        spec->precision = 0;
      } else {
        spec->precision = precision;
      }
      ++p;
    } else {
      spec->precision = parse_int_digits(&p);
    }
  }

  if (*p == 'h' && p[1] == 'h') {
    spec->length = HAL_FMT_LEN_HH;
    p += 2;
  } else if (*p == 'h') {
    spec->length = HAL_FMT_LEN_H;
    ++p;
  } else if (*p == 'l' && p[1] == 'l') {
    spec->length = HAL_FMT_LEN_LL;
    p += 2;
  } else if (*p == 'l') {
    spec->length = HAL_FMT_LEN_L;
    ++p;
  } else if (*p == 'j') {
    spec->length = HAL_FMT_LEN_J;
    ++p;
  } else if (*p == 'z') {
    spec->length = HAL_FMT_LEN_Z;
    ++p;
  } else if (*p == 't') {
    spec->length = HAL_FMT_LEN_T;
    ++p;
  } else if (*p == 'L') {
    spec->length = HAL_FMT_LEN_CAP_L;
    ++p;
  }

  spec->spec = *p;
  if (*p != '\0') {
    ++p;
  }

  size_t pos = 0u;
  append_char(spec->fmt, &pos, sizeof(spec->fmt), '%');
  if (spec->left) {
    append_char(spec->fmt, &pos, sizeof(spec->fmt), '-');
  }
  if (spec->plus) {
    append_char(spec->fmt, &pos, sizeof(spec->fmt), '+');
  }
  if (spec->space) {
    append_char(spec->fmt, &pos, sizeof(spec->fmt), ' ');
  }
  if (spec->alt) {
    append_char(spec->fmt, &pos, sizeof(spec->fmt), '#');
  }
  if (spec->zero) {
    append_char(spec->fmt, &pos, sizeof(spec->fmt), '0');
  }
  if (spec->width_specified) {
    append_uint(spec->fmt, &pos, sizeof(spec->fmt), (unsigned)spec->width);
  }
  if (spec->precision_specified) {
    append_char(spec->fmt, &pos, sizeof(spec->fmt), '.');
    append_uint(spec->fmt, &pos, sizeof(spec->fmt), (unsigned)spec->precision);
  }
  switch (spec->length) {
  case HAL_FMT_LEN_HH:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'h');
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'h');
    break;
  case HAL_FMT_LEN_H:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'h');
    break;
  case HAL_FMT_LEN_L:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'l');
    break;
  case HAL_FMT_LEN_LL:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'l');
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'l');
    break;
  case HAL_FMT_LEN_J:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'j');
    break;
  case HAL_FMT_LEN_Z:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'z');
    break;
  case HAL_FMT_LEN_T:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 't');
    break;
  case HAL_FMT_LEN_CAP_L:
    append_char(spec->fmt, &pos, sizeof(spec->fmt), 'L');
    break;
  default:
    break;
  }
  append_char(spec->fmt, &pos, sizeof(spec->fmt), spec->spec);

  if (pos >= sizeof(spec->fmt)) {
    spec->fmt[sizeof(spec->fmt) - 1u] = '\0';
  } else {
    spec->fmt[pos] = '\0';
  }

  return p;
}

template <typename T>
static void emit_snprintf_arg(hal_fmt_writer_t *w, const char *fmt, T value) {
  char stack[96];
  int n = snprintf(stack, sizeof(stack), fmt, value);
  if (n < 0) {
    return;
  }

  if ((size_t)n < sizeof(stack)) {
    emit_bytes(w, stack, (size_t)n);
    return;
  }

  size_t needed = (size_t)n + 1u;
  char *heap = (char *)malloc(needed);
  if (heap == NULL) {
    emit_bytes(w, stack, sizeof(stack) - 1u);
    return;
  }

  int n2 = snprintf(heap, needed, fmt, value);
  if (n2 > 0) {
    size_t out_len = (size_t)n2;
    if (out_len >= needed) {
      out_len = needed - 1u;
    }
    emit_bytes(w, heap, out_len);
  }
  free(heap);
}

static void emit_string_arg(hal_fmt_writer_t *w, const hal_fmt_spec_t *spec,
                            const char *text) {
  if (text == NULL) {
    text = "(null)";
  }

  size_t len = spec->precision_specified
                   ? bounded_strlen(text, (size_t)spec->precision)
                   : strlen(text);
  size_t width = spec->width_specified ? (size_t)spec->width : 0u;
  size_t pad = width > len ? width - len : 0u;

  if (!spec->left) {
    emit_repeat(w, ' ', pad);
  }
  emit_bytes(w, text, len);
  if (spec->left) {
    emit_repeat(w, ' ', pad);
  }
}

static void emit_char_arg(hal_fmt_writer_t *w, const hal_fmt_spec_t *spec,
                          int ch) {
  char value = (char)ch;
  size_t width = spec->width_specified ? (size_t)spec->width : 0u;
  size_t pad = width > 1u ? width - 1u : 0u;

  if (!spec->left) {
    emit_repeat(w, ' ', pad);
  }
  emit_bytes(w, &value, 1u);
  if (spec->left) {
    emit_repeat(w, ' ', pad);
  }
}

static bool is_signed_integer_spec(char spec) {
  return spec == 'd' || spec == 'i';
}

static bool is_unsigned_integer_spec(char spec) {
  return spec == 'u' || spec == 'o' || spec == 'x' || spec == 'X';
}

static bool is_float_spec(char spec) {
  return spec == 'a' || spec == 'A' || spec == 'e' || spec == 'E' ||
         spec == 'f' || spec == 'F' || spec == 'g' || spec == 'G';
}

static void consume_percent_n(hal_fmt_arg_cursor_t *args) {
  (void)va_arg(args->ap, void *);
}

static void emit_conversion(hal_fmt_writer_t *w, const hal_fmt_spec_t *spec,
                            hal_fmt_arg_cursor_t *args) {
  char code = spec->spec;

  if (code == '\0') {
    emit_bytes(w, "%", 1u);
    return;
  }

  if (code == '%') {
    emit_bytes(w, "%", 1u);
    return;
  }

  if (code == 'n') {
    consume_percent_n(args);
    return;
  }

  if (code == 's' && spec->length == HAL_FMT_LEN_NONE) {
    emit_string_arg(w, spec, va_arg(args->ap, const char *));
    return;
  }

  if (code == 'c' && spec->length == HAL_FMT_LEN_NONE) {
    emit_char_arg(w, spec, va_arg(args->ap, int));
    return;
  }

  if (is_signed_integer_spec(code)) {
    switch (spec->length) {
    case HAL_FMT_LEN_L:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, long));
      break;
    case HAL_FMT_LEN_LL:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, long long));
      break;
    case HAL_FMT_LEN_J:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, intmax_t));
      break;
    case HAL_FMT_LEN_Z:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, ptrdiff_t));
      break;
    case HAL_FMT_LEN_T:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, ptrdiff_t));
      break;
    default:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, int));
      break;
    }
    return;
  }

  if (is_unsigned_integer_spec(code)) {
    switch (spec->length) {
    case HAL_FMT_LEN_L:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, unsigned long));
      break;
    case HAL_FMT_LEN_LL:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, unsigned long long));
      break;
    case HAL_FMT_LEN_J:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, uintmax_t));
      break;
    case HAL_FMT_LEN_Z:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, size_t));
      break;
    case HAL_FMT_LEN_T:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, uintptr_t));
      break;
    default:
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, unsigned int));
      break;
    }
    return;
  }

  if (is_float_spec(code)) {
    if (spec->length == HAL_FMT_LEN_CAP_L) {
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, long double));
    } else {
      emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, double));
    }
    return;
  }

  if (code == 'p') {
    emit_snprintf_arg(w, spec->fmt, va_arg(args->ap, void *));
    return;
  }

  emit_bytes(w, spec->fmt, strlen(spec->fmt));
}

void hal_debug_vformat(hal_debug_format_write_fn write, void *ctx,
                       const char *format, va_list args) {
  hal_fmt_writer_t writer = {write, ctx, 0u};
  if (format == NULL) {
    return;
  }

  hal_fmt_arg_cursor_t cursor;
  va_copy(cursor.ap, args);

  const char *literal = format;
  const char *p = format;

  while (*p != '\0') {
    if (*p != '%') {
      ++p;
      continue;
    }

    if (p > literal) {
      emit_bytes(&writer, literal, (size_t)(p - literal));
    }

    ++p;
    hal_fmt_spec_t spec;
    p = parse_spec(p, &spec, &cursor);
    emit_conversion(&writer, &spec, &cursor);
    literal = p;
  }

  if (p > literal) {
    emit_bytes(&writer, literal, (size_t)(p - literal));
  }

  va_end(cursor.ap);
}

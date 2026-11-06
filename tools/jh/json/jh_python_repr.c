#include "jh_python_repr.h"
#include <jb/json_number.h>
#include <jb/json_writer.h>
#include <jb/unicode.h>
#include <jb/utf8.h>
#include <jb/vector.h>
#include <stdio.h>
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

static hal_status_t append_literal(jb_string_t *out, const char *text) {
  return jb_string_append(out, text, strlen(text));
}

static bool span_is(jb_json_span_t text, const char *literal) {
  const size_t length = strlen(literal);
  return text.length == length && memcmp(text.data, literal, length) == 0;
}

/* Whether json.loads makes an int of a number token: no fraction, no
 * exponent and not one of its non-finite constants. */
static bool integer_token(jb_json_span_t token) {
  return token.length != 0 && !memchr(token.data, '.', token.length) &&
         !memchr(token.data, 'e', token.length) &&
         !memchr(token.data, 'E', token.length) && !span_is(token, "NaN") &&
         !span_is(token, "Infinity") && !span_is(token, "-Infinity");
}

/* Resize a byte scratch vector and lend it as a workspace. */
static hal_status_t workspace(jb_vector_t *scratch, size_t bytes,
                              jb_json_workspace_t *out) {
  const hal_status_t status = jb_vector_resize(scratch, bytes);
  if (status == HAL_OK) {
    out->data = jb_vector_data(scratch);
    out->size = jb_vector_size(scratch);
  }
  return status;
}

/* The binary64 bits of a finite number token within the binary64 range; the
 * conversion budget suits the short tokens of decimal_form. */
static hal_status_t token_bits(jb_json_span_t token, uint64_t *bits) {
  const jb_json_limits_t limits = {token.length, 1, 1, 0, token.length, 0, 1};
  jb_vector_t scratch = {0};
  jb_json_token_t parsed[1];
  jb_json_doc_t doc = {0};
  jb_json_workspace_t work = {NULL, 0};
  size_t bytes = 0;
  hal_status_t status = jb_vector_init(&scratch, 1, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_json_parse_workspace_size(&limits, &bytes);
  if (status == HAL_OK)
    status = workspace(&scratch, bytes, &work);
  if (status == HAL_OK)
    status = jb_json_parse(token, &limits, parsed, 1, work, &doc, NULL);
  const jb_json_node_t node = {&doc, 0};
  if (status == HAL_OK)
    status = jb_json_float_workspace_size(node, 64, &bytes);
  if (status == HAL_OK)
    status = workspace(&scratch, bytes, &work);
  const jb_json_float_options_t options = {false, false,
                                           64 * token.length + 1000000};
  if (status == HAL_OK)
    status = jb_json_get_f64_bits(node, &options, work, bits);
  jb_vector_destroy(&scratch);
  return status;
}

/* The shortest round-trip spelling of a finite value as a JSON number. */
static hal_status_t shortest(double value, char *buffer, size_t capacity,
                             size_t *length) {
  const jb_json_writer_limits_t limits = {capacity, 1, 0, 0, 0};
  jb_vector_t scratch = {0};
  jb_json_workspace_t work = {NULL, 0};
  jb_json_buffer_t sink = {buffer, capacity, 0};
  jb_json_writer_t writer = {NULL, HAL_NONE};
  size_t bytes = 0;
  hal_status_t status = jb_vector_init(&scratch, 1, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_json_writer_workspace_size(&limits, &bytes);
  if (status == HAL_OK)
    status = workspace(&scratch, bytes, &work);
  if (status == HAL_OK)
    status =
        jb_json_writer_init(&writer, &limits, work, jb_json_buffer_sink, &sink);
  if (status == HAL_OK)
    status = jb_json_writer_f64(&writer, value);
  if (status == HAL_OK)
    status = jb_json_writer_finish(&writer, length);
  jb_vector_destroy(&scratch);
  return status;
}

/* repr() of a nonzero float from its shortest spelling: the significant
 * digits and the decimal point position, in fixed notation for decimal
 * points from -3 to 16 and in exponent notation otherwise. */
static hal_status_t append_float_digits(jb_string_t *out, const char *spelled,
                                        size_t length) {
  char digits[32];
  size_t count = 0, at = 0;
  long point = 0, exponent = 0;
  bool negative = false, exponent_negative = false, fraction = false;
  if (at < length && spelled[at] == '-') {
    negative = true;
    ++at;
  }
  for (; at < length && spelled[at] != 'e' && spelled[at] != 'E'; ++at) {
    if (spelled[at] == '.') {
      fraction = true;
      continue;
    }
    if (count == sizeof digits)
      return HAL_EOVERFLOW;
    digits[count++] = spelled[at];
    if (!fraction)
      ++point;
  }
  if (at < length) {
    ++at;
    if (at < length && (spelled[at] == '-' || spelled[at] == '+'))
      exponent_negative = spelled[at++] == '-';
    for (; at < length; ++at)
      exponent = exponent * 10 + (spelled[at] - '0');
  }
  point += exponent_negative ? -exponent : exponent;
  size_t first = 0;
  while (first < count && digits[first] == '0') {
    ++first;
    --point;
  }
  while (count > first && digits[count - 1] == '0')
    --count;
  const char *significant = digits + first;
  const size_t used = count - first;
  const size_t whole = point > 0 ? (size_t)point : 0;
  hal_status_t status = negative ? append_literal(out, "-") : HAL_OK;
  if (status != HAL_OK)
    return status;
  if (point <= -4 || point > 16) {
    char exponent_text[16];
    const long shown = point - 1;
    const long magnitude = shown < 0 ? -shown : shown;
    const int written = snprintf(exponent_text, sizeof exponent_text,
                                 "e%c%02ld", shown < 0 ? '-' : '+', magnitude);
    status = jb_string_append(out, significant, 1);
    if (status == HAL_OK && used > 1)
      status = append_literal(out, ".");
    if (status == HAL_OK && used > 1)
      status = jb_string_append(out, significant + 1, used - 1);
    return status == HAL_OK
               ? jb_string_append(out, exponent_text, (size_t)written)
               : status;
  }
  if (point <= 0) {
    status = append_literal(out, "0.");
    for (long zeros = point; status == HAL_OK && zeros < 0; ++zeros)
      status = append_literal(out, "0");
    return status == HAL_OK ? jb_string_append(out, significant, used) : status;
  }
  if (whole >= used) {
    status = jb_string_append(out, significant, used);
    for (size_t zeros = used; status == HAL_OK && zeros < whole; ++zeros)
      status = append_literal(out, "0");
    return status == HAL_OK ? append_literal(out, ".0") : status;
  }
  status = jb_string_append(out, significant, whole);
  if (status == HAL_OK)
    status = append_literal(out, ".");
  return status == HAL_OK
             ? jb_string_append(out, significant + whole, used - whole)
             : status;
}

/* Significant digits kept for the conversion. Every binary64 rounding
 * boundary has at most 767 significant digits, so the first 768 digits and
 * whether a nonzero digit follows decide the rounding. */
#define KEPT_DIGITS 768u

/* A finite number token as 0.<digits> x 10^exponent: the significant digits
 * from the first nonzero one, at most KEPT_DIGITS of them, and whether a
 * nonzero digit was dropped. The exponent saturates far outside the binary64
 * range. */
typedef struct {
  bool negative, sticky;
  char digits[KEPT_DIGITS];
  size_t count;
  long long exponent;
} decimal_t;

static const long long exponent_limit = 1000000000000000LL;

static long long saturated(long long value) {
  return value > exponent_limit    ? exponent_limit
         : value < -exponent_limit ? -exponent_limit
                                   : value;
}

static void decimal_form(jb_json_span_t token, decimal_t *out) {
  size_t at = 0;
  out->negative = token.length != 0 && token.data[0] == '-';
  out->sticky = false;
  out->count = 0;
  at += out->negative ? 1u : 0u;
  long long point = 0; /* digits of the integer part from the first nonzero */
  bool fraction = false, started = false;
  for (; at < token.length && token.data[at] != 'e' && token.data[at] != 'E';
       ++at) {
    const char digit = token.data[at];
    if (digit == '.') {
      fraction = true;
      continue;
    }
    started = started || digit != '0';
    if (!started) {
      point -= fraction ? 1 : 0;
      continue;
    }
    point += fraction ? 0 : 1;
    if (out->count < KEPT_DIGITS)
      out->digits[out->count++] = digit;
    else
      out->sticky = out->sticky || digit != '0';
  }
  long long exponent = 0;
  bool negative_exponent = false;
  if (at < token.length) {
    ++at;
    negative_exponent = at < token.length && token.data[at] == '-';
    at += at < token.length && (token.data[at] == '-' || token.data[at] == '+')
              ? 1u
              : 0u;
    for (; at < token.length; ++at)
      exponent = saturated(exponent * 10 + (token.data[at] - '0'));
  }
  out->exponent =
      saturated(saturated(point) + (negative_exponent ? -exponent : exponent));
}

/* Whether |value| >= 2^1024 - 2^970, where rounding gives an infinity: the
 * midpoint above the largest binary64 value rounds to the even 2^1024. */
static bool overflows(const decimal_t *value) {
  static const char bound[] =
      "17976931348623158079372897140530341507993413271003782693617377898044496"
      "82927647509466490179775872070963302864166928879109465555478519404026306"
      "57488671505820681908902000708383676273854845817711531764475730270069855"
      "57136695962284291481986083493647529271907416844436551070434271155969950"
      "8093042880177904174497792";
  const size_t length = sizeof bound - 1;
  if (value->exponent != (long long)length)
    return value->exponent > (long long)length;
  for (size_t i = 0; i < length; ++i) {
    const char digit = i < value->count ? value->digits[i] : '0';
    if (digit != bound[i])
      return digit > bound[i];
  }
  return true;
}

hal_status_t jh_python_float(jb_json_span_t token, double *out) {
  if (token.data == NULL || out == NULL)
    return HAL_EINVAL;
  if (span_is(token, "NaN")) {
    const uint64_t bits = 0x7ff8000000000000u;
    memcpy(out, &bits, sizeof *out);
    return HAL_OK;
  }
  if (span_is(token, "Infinity") || span_is(token, "-Infinity")) {
    const uint64_t bits =
        token.data[0] == '-' ? 0xfff0000000000000u : 0x7ff0000000000000u;
    memcpy(out, &bits, sizeof *out);
    return HAL_OK;
  }
  // The range is decided here and the conversion gets at most KEPT_DIGITS
  // digits: an exhausted conversion budget is an error, never an infinity.
  decimal_t decimal = {0};
  decimal_form(token, &decimal);
  uint64_t bits = 0;
  if (decimal.count == 0 || decimal.exponent < -400) {
    bits = decimal.negative ? 0x8000000000000000u : 0u;
  } else if (overflows(&decimal)) {
    bits = decimal.negative ? 0xfff0000000000000u : 0x7ff0000000000000u;
  } else {
    char canonical[KEPT_DIGITS + 32];
    size_t used = 0;
    if (decimal.negative)
      canonical[used++] = '-';
    canonical[used++] = '0';
    canonical[used++] = '.';
    memcpy(canonical + used, decimal.digits, decimal.count);
    used += decimal.count;
    if (decimal.sticky)
      canonical[used++] = '1';
    used += (size_t)snprintf(canonical + used, sizeof canonical - used, "e%lld",
                             decimal.exponent);
    const jb_json_span_t form = {canonical, used};
    const hal_status_t converted = token_bits(form, &bits);
    if (converted != HAL_OK)
      return converted;
  }
  memcpy(out, &bits, sizeof *out);
  return HAL_OK;
}

/* repr() of a float appended to out, possibly in parts. */
static hal_status_t append_float(jb_string_t *out, double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof bits);
  const bool negative = (bits >> 63) != 0;
  if ((bits & 0x7ff0000000000000u) == 0x7ff0000000000000u)
    return append_literal(out, (bits & 0x000fffffffffffffu) != 0 ? "nan"
                               : negative                        ? "-inf"
                                                                 : "inf");
  if ((bits & 0x7fffffffffffffffu) == 0)
    return append_literal(out, negative ? "-0.0" : "0.0");
  char spelled[64];
  size_t length = 0;
  const hal_status_t status = shortest(value, spelled, sizeof spelled, &length);
  return status == HAL_OK ? append_float_digits(out, spelled, length) : status;
}

hal_status_t jh_python_float_append(jb_string_t *out, double value) {
  if (out == NULL)
    return HAL_EINVAL;
  // Built aside, so a failure leaves out as it was.
  jb_string_t spelled = {0};
  hal_status_t status = jb_string_init(&spelled, &out->allocator);
  if (status == HAL_OK)
    status = append_float(&spelled, value);
  if (status == HAL_OK)
    status = jb_string_append(out, jb_string_data(&spelled),
                              jb_string_size(&spelled));
  jb_string_destroy(&spelled);
  return status;
}

/* repr() of the int or float json.loads makes of a number token. */
static hal_status_t append_number(jb_string_t *out, jb_json_span_t token) {
  if (integer_token(token))
    return span_is(token, "-0")
               ? append_literal(out, "0")
               : jb_string_append(out, token.data, token.length);
  double value = 0.0;
  const hal_status_t converted = jh_python_float(token, &value);
  return converted == HAL_OK ? append_float(out, value) : converted;
}

static hal_status_t append_value(jb_string_t *out, jb_json_tree_view_t value);

/* The members of an object or the items of an array, separated by ", ". */
static hal_status_t append_items(jb_string_t *out, jb_json_tree_view_t value,
                                 bool object) {
  size_t count = 0;
  hal_status_t status = jb_json_tree_count(value, &count);
  for (size_t index = 0; status == HAL_OK && index < count; ++index) {
    jb_json_tree_view_t item = {0};
    status = jb_json_tree_at(value, index, &item);
    if (status == HAL_OK && index != 0)
      status = append_literal(out, ", ");
    if (status == HAL_OK && object) {
      jb_json_span_t key = {NULL, 0};
      status = jb_json_tree_key(item, &key);
      if (status == HAL_OK)
        status = jh_python_repr(key, append_piece, out);
      if (status == HAL_OK)
        status = append_literal(out, ": ");
    }
    if (status == HAL_OK)
      status = append_value(out, item);
  }
  return status;
}

static hal_status_t append_value(jb_string_t *out, jb_json_tree_view_t value) {
  jb_json_type_t type = JB_JSON_NULL;
  hal_status_t status = jb_json_tree_type(value, &type);
  if (status != HAL_OK)
    return status;
  jb_json_span_t text = {NULL, 0};
  bool flag = false;
  switch (type) {
  case JB_JSON_NULL:
    return append_literal(out, "None");
  case JB_JSON_BOOL:
    status = jb_json_tree_get_bool(value, &flag);
    return status == HAL_OK ? append_literal(out, flag ? "True" : "False")
                            : status;
  case JB_JSON_NUMBER:
    status = jb_json_tree_text(value, &text);
    return status == HAL_OK ? append_number(out, text) : status;
  case JB_JSON_STRING:
    status = jb_json_tree_text(value, &text);
    return status == HAL_OK ? jh_python_repr(text, append_piece, out) : status;
  case JB_JSON_ARRAY:
    status = append_literal(out, "[");
    if (status == HAL_OK)
      status = append_items(out, value, false);
    return status == HAL_OK ? append_literal(out, "]") : status;
  default:
    status = append_literal(out, "{");
    if (status == HAL_OK)
      status = append_items(out, value, true);
    return status == HAL_OK ? append_literal(out, "}") : status;
  }
}

hal_status_t jh_python_repr_json_append(jb_string_t *out,
                                        jb_json_tree_view_t value) {
  if (out == NULL)
    return HAL_EINVAL;
  // Built aside, so a failure leaves out as it was.
  jb_string_t quoted = {0};
  hal_status_t status = jb_string_init(&quoted, &out->allocator);
  if (status == HAL_OK)
    status = append_value(&quoted, value);
  if (status == HAL_OK)
    status =
        jb_string_append(out, jb_string_data(&quoted), jb_string_size(&quoted));
  jb_string_destroy(&quoted);
  return status;
}

bool jh_python_strip(jb_json_span_t text, jb_json_span_t *out) {
  size_t begin = 0, end = 0;
  bool content = false;
  for (size_t at = 0; at < text.length;) {
    uint32_t scalar = 0;
    size_t used = 0;
    jb_unicode_properties_t properties = {0};
    if (jb_utf8_decode(text.data + at, text.length - at, &scalar, &used) !=
            HAL_OK ||
        jb_unicode_properties(scalar, &properties) != HAL_OK)
      return false;
    if (!properties.whitespace) {
      if (!content)
        begin = at;
      content = true;
      end = at + used;
    }
    at += used;
  }
  out->data = text.data + begin;
  out->length = content ? end - begin : 0;
  return true;
}

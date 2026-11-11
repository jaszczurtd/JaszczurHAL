#include "jh_config_expression.h"
#include "jh_python_repr.h"
#include <jb/unicode.h>
#include <jb/utf8.h>
#include <string.h>

bool jh_config_digit(char c) { return c >= '0' && c <= '9'; }
bool jh_config_name_start(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool jh_config_name_part(char c) {
  return jh_config_name_start(c) || jh_config_digit(c);
}
bool jh_config_identifier(jb_json_span_t text) {
  if (text.length == 0 || !jh_config_name_start(text.data[0]))
    return false;
  for (size_t i = 1; i < text.length; ++i)
    if (!jh_config_name_part(text.data[i]))
      return false;
  return true;
}

uint32_t jh_config_scalar_at(jb_json_span_t text, size_t offset,
                             size_t *length) {
  uint32_t scalar = 0;
  if (jb_utf8_decode(text.data + offset, text.length - offset, &scalar,
                     length) != HAL_OK) {
    scalar = (unsigned char)text.data[offset];
    *length = 1;
  }
  return scalar;
}

bool jh_config_space_at(jb_json_span_t text, size_t offset, size_t *length) {
  const unsigned char byte = (unsigned char)text.data[offset];
  if (byte < 0x80) {
    *length = 1;
    return (byte >= 0x09 && byte <= 0x0D) || (byte >= 0x1C && byte <= 0x20);
  }
  const uint32_t scalar = jh_config_scalar_at(text, offset, length);
  jb_unicode_properties_t properties;
  return jb_unicode_properties(scalar, &properties) == HAL_OK &&
         properties.whitespace;
}

size_t jh_config_skip_space(jb_json_span_t text, size_t offset) {
  size_t used;
  while (offset < text.length && jh_config_space_at(text, offset, &used))
    offset += used;
  return offset;
}

jb_json_span_t jh_config_rstrip(jb_json_span_t text) {
  size_t end = text.length, used;
  while (end > 0) {
    size_t start = end - 1;
    while (start > 0 && ((unsigned char)text.data[start] & 0xC0) == 0x80)
      --start;
    if (!jh_config_space_at(text, start, &used))
      break;
    end = start;
  }
  text.length = end;
  return text;
}

static bool suffix(char c) {
  return c == 'u' || c == 'U' || c == 'l' || c == 'L';
}
static size_t literal_suffix(jb_json_span_t text, size_t i) {
  while (i < text.length && suffix(text.data[i]))
    ++i;
  return i;
}

/* Alternatives in the order of _TOKEN in the former Python reader. */
static size_t token_end(jb_json_span_t text, size_t i) {
  const size_t size = text.length;
  const char *bytes = text.data;
  const char c = bytes[i], next = i + 1 < size ? bytes[i + 1] : 0;
  if (c == '0' && (next == 'x' || next == 'X') && i + 2 < size &&
      jh_hex_digit(bytes[i + 2]) >= 0) {
    size_t j = i + 2;
    while (j < size && jh_hex_digit(bytes[j]) >= 0)
      ++j;
    return literal_suffix(text, j);
  }
  if (c == '0' && (next == 'b' || next == 'B') && i + 2 < size &&
      (bytes[i + 2] == '0' || bytes[i + 2] == '1')) {
    size_t j = i + 2;
    while (j < size && (bytes[j] == '0' || bytes[j] == '1'))
      ++j;
    return literal_suffix(text, j);
  }
  if (jh_config_digit(c)) {
    size_t j = i;
    while (j < size && jh_config_digit(bytes[j]))
      ++j;
    return literal_suffix(text, j);
  }
  if (c == '\'') {
    size_t j = i + 1, items = 0;
    while (j < size && bytes[j] != '\'') {
      if (bytes[j] == '\\' && ++j == size)
        break;
      size_t used;
      (void)jh_config_scalar_at(text, j, &used);
      j += used;
      ++items;
    }
    if (items != 0 && j < size && bytes[j] == '\'')
      return j + 1;
  }
  if (jh_config_name_start(c)) {
    size_t j = i + 1;
    while (j < size && jh_config_name_part(bytes[j]))
      ++j;
    return j;
  }
  static const char pairs[][3] = {
      "||", "&&", "==", "!=", "<=", ">=", "<<", ">>"};
  for (size_t j = 0; j < COUNTOF(pairs); ++j)
    if (c == pairs[j][0] && next == pairs[j][1])
      return i + 2;
  return c != 0 && strchr("-+*/%<>!~&|^?:(),", c) != NULL ? i + 1 : i;
}

static hal_status_t quoted_failure(jh_failure_t *failure, hal_status_t status,
                                   const char *prefix, jb_json_span_t token,
                                   const char *after) {
  jb_string_t detail = {0};
  hal_status_t storage = jb_string_init(&detail, jb_heap_allocator());
  jh_put(&detail, &storage, prefix);
  if (storage == HAL_OK)
    storage = jh_python_repr_append(&detail, token);
  jh_put(&detail, &storage, after);
  const hal_status_t result =
      storage == HAL_OK
          ? jh_fail(failure, status, 2, JH_PARTS(jh_string_span(&detail)))
          : jh_fail_storage(failure, storage);
  jb_string_destroy(&detail);
  return result;
}

hal_status_t jh_config_tokenize(jb_json_span_t input, jb_vector_t *tokens,
                                jh_failure_t *failure) {
  if (tokens == NULL || failure == NULL ||
      (input.data == NULL && input.length != 0))
    return jh_fail_arguments(failure);
  const size_t begin = jh_config_skip_space(input, 0);
  jb_json_span_t text = {input.data == NULL ? "" : input.data + begin,
                         input.length - begin};
  text = jh_config_rstrip(text);
  jb_vector_t result = {0};
  hal_status_t status = jh_list_init(&result);
  for (size_t offset = 0; status == HAL_OK && offset < text.length;) {
    const size_t start = jh_config_skip_space(text, offset);
    const size_t end = start < text.length ? token_end(text, start) : start;
    if (end == start) {
      const jb_json_span_t rest = {text.data + offset, text.length - offset};
      status =
          quoted_failure(failure, HAL_ECONFIG, "cannot evaluate ", rest, "");
      jh_list_destroy(&result);
      return status;
    }
    const jb_json_span_t token = {text.data + start, end - start};
    status = jh_list_push(&result, token);
    offset = end;
  }
  if (status == HAL_OK) {
    jh_list_destroy(tokens);
    *tokens = result;
  } else {
    jh_list_destroy(&result);
    jh_fail_storage(failure, status);
  }
  return status;
}

typedef struct {
  const jb_json_span_t *tokens;
  size_t count, index, nesting;
  hal_status_t status;
  bool range_error;
  jh_failure_t *failure;
} parser_t;

static bool is(jb_json_span_t token, const char *text) {
  return jh_span_equal(token, jh_span(text));
}
static jb_json_span_t peek(const parser_t *p) {
  const jb_json_span_t empty = {"", 0};
  return p->index < p->count ? p->tokens[p->index] : empty;
}
static void fail(parser_t *p, const char *text) {
  p->status = jh_fail_text(p->failure, HAL_ECONFIG, 2, text);
}
static jb_json_span_t take(parser_t *p, const char *expected) {
  const jb_json_span_t token = peek(p);
  if (p->status != HAL_OK)
    return token;
  if (p->index == p->count || (expected != NULL && !is(token, expected))) {
    p->status = jh_fail(p->failure, HAL_ECONFIG, 2,
                        JH_PARTS(jh_span("expected "),
                                 jh_span(expected ? expected : "operand"),
                                 jh_span(" in condition")));
    return token;
  }
  ++p->index;
  return token;
}
static void unexpected(parser_t *p, jb_json_span_t token) {
  p->status = quoted_failure(p->failure, HAL_ECONFIG, "unexpected ", token,
                             " in condition");
}
static bool enter(parser_t *p) {
  if (++p->nesting <= 256)
    return true;
  p->status = jh_fail_text(p->failure, HAL_EOVERFLOW, 4,
                           "condition nests more than 256 levels");
  return false;
}
static void range(parser_t *p, jb_json_span_t detail) {
  p->status = jh_fail(p->failure, HAL_EOVERFLOW, 4, &detail, 1);
  p->range_error =
      p->status == HAL_EOVERFLOW && jb_string_size(&p->failure->message) != 0;
}
static int64_t arithmetic_range(parser_t *p, int64_t left, jb_json_span_t op,
                                int64_t right) {
  jb_string_t detail = {0};
  hal_status_t status = jb_string_init(&detail, jb_heap_allocator());
  jh_put_int64(&detail, &status, left);
  jh_put(&detail, &status, " ");
  jh_put_span(&detail, &status, op);
  jh_put(&detail, &status, " ");
  jh_put_int64(&detail, &status, right);
  if (status == HAL_OK)
    range(p, jh_string_span(&detail));
  else
    p->status = jh_fail_storage(p->failure, status);
  jb_string_destroy(&detail);
  return 0;
}

static uint32_t character_value(jb_json_span_t token) {
  size_t used;
  if (token.data[1] != '\\')
    return jh_config_scalar_at(token, 1, &used);
  switch (token.data[2]) {
  case 'n':
    return 10;
  case 't':
    return 9;
  case 'r':
    return 13;
  case '0':
    return 0;
  case '\\':
    return 92;
  case '\'':
    return 39;
  case '"':
    return 34;
  default:
    return jh_config_scalar_at(token, 2, &used);
  }
}
static int64_t integer_literal(parser_t *p, jb_json_span_t token, bool live) {
  if (token.data[0] == '\'')
    return (int64_t)character_value(token);
  size_t end = token.length, start = 0;
  while (end != 0 && suffix(token.data[end - 1]))
    --end;
  unsigned base = 10;
  if (end > 1 && token.data[0] == '0') {
    if (token.data[1] == 'x' || token.data[1] == 'X') {
      base = 16;
      start = 2;
    } else if (token.data[1] == 'b' || token.data[1] == 'B') {
      base = 2;
      start = 2;
    } else {
      base = 8;
    }
  }
  uint64_t value = 0;
  bool in_range = true;
  for (size_t i = start; i < end; ++i) {
    const unsigned digit = (unsigned)jh_hex_digit(token.data[i]);
    if (digit >= base) {
      /* An invalid octal literal stays an error even in a skipped operand. */
      p->status = quoted_failure(p->failure, HAL_EPROTO,
                                 "invalid octal literal ", token, "");
      return 0;
    }
    if (value > ((uint64_t)INT64_MAX - digit) / base)
      in_range = false;
    else
      value = value * base + digit;
  }
  if (in_range)
    return (int64_t)value;
  if (live) {
    p->status = jh_fail(p->failure, HAL_EOVERFLOW, 4,
                        JH_PARTS(jh_span("integer "), token));
    p->range_error =
        p->status == HAL_EOVERFLOW && jb_string_size(&p->failure->message) != 0;
  }
  return 0;
}

static int64_t from_bits(uint64_t bits) {
  return bits <= (uint64_t)INT64_MAX ? (int64_t)bits : -(int64_t)(~bits) - 1;
}
static int64_t shift(parser_t *p, jb_json_span_t op, int64_t left,
                     int64_t right) {
  bool to_right = is(op, ">>");
  uint64_t count;
  if (right >= 0) {
    count = (uint64_t)right;
  } else {
    /* Negative counts follow GCC's preprocessor extension. */
    to_right = !to_right;
    count = (uint64_t)(-(right + 1)) + 1;
  }
  if (to_right) {
    if (count >= 64)
      return left < 0 ? -1 : 0;
    return left >= 0 ? left >> count : ~((~left) >> count);
  }
  if (left == 0)
    return 0;
  if (count > 63)
    return arithmetic_range(p, left, op, right);
  const int64_t largest = INT64_MAX >> count;
  if (left > largest || left < -largest - 1)
    return arithmetic_range(p, left, op, right);
  return from_bits((uint64_t)left << count);
}
static bool multiply_overflows(int64_t left, int64_t right) {
  if (left == 0 || right == 0)
    return false;
  if (left > 0)
    return right > 0 ? left > INT64_MAX / right : right < INT64_MIN / left;
  return right > 0 ? left < INT64_MIN / right : right < INT64_MAX / left;
}
static int64_t apply(parser_t *p, jb_json_span_t op, int64_t left,
                     int64_t right, bool live) {
  if (!live || p->status != HAL_OK)
    return 0;
  if ((is(op, "/") || is(op, "%")) && right == 0) {
    fail(p, "division by zero in condition");
    return 0;
  }
  if (is(op, "<<") || is(op, ">>"))
    return shift(p, op, left, right);
  if (is(op, "/")) {
    if (left == INT64_MIN && right == -1)
      return arithmetic_range(p, left, op, right);
    return left / right;
  }
  if (is(op, "%"))
    return right == -1 ? 0 : left % right;
  if (is(op, "+")) {
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right))
      return arithmetic_range(p, left, op, right);
    return left + right;
  }
  if (is(op, "-")) {
    if ((right < 0 && left > INT64_MAX + right) ||
        (right > 0 && left < INT64_MIN + right))
      return arithmetic_range(p, left, op, right);
    return left - right;
  }
  if (is(op, "*")) {
    if (multiply_overflows(left, right))
      return arithmetic_range(p, left, op, right);
    return left * right;
  }
  if (is(op, "|"))
    return from_bits((uint64_t)left | (uint64_t)right);
  if (is(op, "^"))
    return from_bits((uint64_t)left ^ (uint64_t)right);
  if (is(op, "&"))
    return from_bits((uint64_t)left & (uint64_t)right);
  if (is(op, "||"))
    return left != 0 || right != 0;
  if (is(op, "&&"))
    return left != 0 && right != 0;
  if (is(op, "=="))
    return left == right;
  if (is(op, "!="))
    return left != right;
  if (is(op, "<"))
    return left < right;
  if (is(op, ">"))
    return left > right;
  if (is(op, "<="))
    return left <= right;
  return left >= right;
}

static int64_t conditional(parser_t *p, bool live);
static int64_t unary(parser_t *p, bool live) {
  const jb_json_span_t token = take(p, NULL);
  if (p->status != HAL_OK)
    return 0;
  if (is(token, "!") || is(token, "~") || is(token, "-") || is(token, "+") ||
      is(token, "(")) {
    if (!enter(p))
      return 0;
    const bool parenthesis = is(token, "(");
    const int64_t value = parenthesis ? conditional(p, live) : unary(p, live);
    if (parenthesis)
      (void)take(p, ")");
    --p->nesting;
    if (p->status != HAL_OK)
      return 0;
    if (is(token, "!"))
      return value == 0;
    if (is(token, "~"))
      return ~value;
    if (is(token, "-")) {
      if (!live)
        return 0;
      if (value == INT64_MIN) {
        range(p, jh_span("-(-9223372036854775808)"));
        return 0;
      }
      return -value;
    }
    return value;
  }
  if (jh_config_digit(token.data[0]) || token.data[0] == '\'')
    return integer_literal(p, token, live);
  if (!jh_config_identifier(token))
    unexpected(p, token);
  return 0;
}

static const char *const levels[][4] = {{"||", NULL},
                                        {"&&", NULL},
                                        {"|", NULL},
                                        {"^", NULL},
                                        {"&", NULL},
                                        {"==", "!=", NULL},
                                        {"<", ">", "<=", ">="},
                                        {"<<", ">>", NULL},
                                        {"+", "-", NULL},
                                        {"*", "/", "%", NULL}};
static bool at_level(size_t level, jb_json_span_t op) {
  for (size_t i = 0; i < COUNTOF(levels[0]) && levels[level][i] != NULL; ++i)
    if (is(op, levels[level][i]))
      return true;
  return false;
}
static int64_t binary(parser_t *p, size_t level, bool live) {
  if (level == COUNTOF(levels))
    return unary(p, live);
  int64_t value = binary(p, level + 1, live);
  while (p->status == HAL_OK && p->index < p->count &&
         at_level(level, peek(p))) {
    const jb_json_span_t op = take(p, NULL);
    const bool decided =
        (is(op, "&&") && value == 0) || (is(op, "||") && value != 0);
    const int64_t right = binary(p, level + 1, live && !decided);
    value = apply(p, op, value, right, live);
  }
  return value;
}
static int64_t conditional(parser_t *p, bool live) {
  const int64_t condition = binary(p, 0, live);
  if (p->status != HAL_OK || !is(peek(p), "?"))
    return condition;
  (void)take(p, "?");
  if (!enter(p))
    return 0;
  const int64_t when_true = conditional(p, live && condition != 0);
  (void)take(p, ":");
  const int64_t when_false =
      p->status == HAL_OK ? conditional(p, live && condition == 0) : 0;
  --p->nesting;
  return condition != 0 ? when_true : when_false;
}

hal_status_t jh_config_expression(const jb_json_span_t *tokens, size_t count,
                                  int64_t *value, bool *range_error,
                                  jh_failure_t *failure) {
  if (range_error != NULL)
    *range_error = false;
  if ((tokens == NULL && count != 0) || value == NULL || range_error == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  for (size_t i = 0; i < count; ++i)
    if (tokens[i].data == NULL || tokens[i].length == 0)
      return jh_fail_arguments(failure);
  parser_t parser = {tokens, count, 0, 0, HAL_OK, false, failure};
  const int64_t result = conditional(&parser, true);
  if (parser.status == HAL_OK && parser.index != count)
    unexpected(&parser, peek(&parser));
  if (parser.status == HAL_OK)
    *value = result;
  *range_error = parser.range_error;
  return parser.status;
}

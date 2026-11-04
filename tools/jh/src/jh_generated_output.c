#include "jh_generated_output.h"
#include <jb/file.h>
#include <jb/vector.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { op_equal, op_remove, op_insert };

/* One run of equal, removed or inserted lines: [a_begin, a_end) of the old
 * sequence against [b_begin, b_end) of the new one. */
typedef struct {
  unsigned char tag;
  size_t a_begin, a_end, b_begin, b_end;
} opcode_t;

static size_t smaller(size_t a, size_t b) { return a < b ? a : b; }
static size_t larger(size_t a, size_t b) { return a > b ? a : b; }

/* Start a zero vector of element_size elements on the host heap. */
static hal_status_t vector_of(jb_vector_t *vector, size_t element_size) {
  return jb_vector_init(vector, element_size, jb_heap_allocator());
}

static hal_status_t repeat(jb_vector_t *script, unsigned char tag,
                           size_t count) {
  hal_status_t status = HAL_OK;
  for (size_t index = 0; status == HAL_OK && index < count; ++index)
    status = jb_vector_append(script, &tag, 1);
  return status;
}

/* Edit script of the middle of the sequences, between a common prefix and
 * suffix: a longest common subsequence while its table stays small; larger
 * middles are reported as one removal and one insertion. */
static hal_status_t edit_script(const jb_json_span_t *a, size_t a_count,
                                const jb_json_span_t *b, size_t b_count,
                                size_t prefix, size_t suffix,
                                jb_vector_t *script) {
  const size_t n = a_count - prefix - suffix, m = b_count - prefix - suffix;
  const size_t table_limit = 4000000;
  if (n == 0 || m == 0 || m > table_limit / n) {
    const hal_status_t status = repeat(script, op_remove, n);
    return status == HAL_OK ? repeat(script, op_insert, m) : status;
  }
  const size_t width = m + 1;
  jb_vector_t table = {0};
  hal_status_t status = vector_of(&table, sizeof(uint32_t));
  if (status == HAL_OK)
    status = jb_vector_resize(&table, (n + 1) * width);
  if (status == HAL_OK) {
    uint32_t *cell = jb_vector_data(&table);
    for (size_t i = n; i-- > 0;)
      for (size_t j = m; j-- > 0;) {
        const uint32_t down = cell[(i + 1) * width + j];
        const uint32_t right = cell[i * width + j + 1];
        cell[i * width + j] = jh_span_equal(a[prefix + i], b[prefix + j])
                                  ? cell[(i + 1) * width + j + 1] + 1
                                  : (down > right ? down : right);
      }
    size_t i = 0, j = 0;
    while (status == HAL_OK && i < n && j < m) {
      unsigned char tag;
      if (jh_span_equal(a[prefix + i], b[prefix + j])) {
        tag = op_equal;
        ++i;
        ++j;
      } else if (cell[(i + 1) * width + j] >= cell[i * width + j + 1]) {
        tag = op_remove;
        ++i;
      } else {
        tag = op_insert;
        ++j;
      }
      status = jb_vector_append(script, &tag, 1);
    }
    if (status == HAL_OK)
      status = repeat(script, op_remove, n - i);
    if (status == HAL_OK)
      status = repeat(script, op_insert, m - j);
  }
  jb_vector_destroy(&table);
  return status;
}

/* Extend the last run when it has the same tag, otherwise start a new one. */
static hal_status_t add_opcode(jb_vector_t *codes, unsigned char tag,
                               size_t a_count, size_t b_count) {
  const size_t count = jb_vector_size(codes);
  opcode_t *last = count == 0 ? NULL : jb_vector_at(codes, count - 1);
  if (last != NULL && last->tag == tag) {
    last->a_end += a_count;
    last->b_end += b_count;
    return HAL_OK;
  }
  const size_t a_at = last == NULL ? 0 : last->a_end;
  const size_t b_at = last == NULL ? 0 : last->b_end;
  const opcode_t code = {tag, a_at, a_at + a_count, b_at, b_at + b_count};
  return jb_vector_append(codes, &code, 1);
}

/* Runs of the edit between the sequences, in order. */
static hal_status_t opcodes(const jb_json_span_t *a, size_t a_count,
                            const jb_json_span_t *b, size_t b_count,
                            jb_vector_t *codes) {
  size_t prefix = 0;
  while (prefix < a_count && prefix < b_count &&
         jh_span_equal(a[prefix], b[prefix]))
    ++prefix;
  size_t suffix = 0;
  while (suffix < a_count - prefix && suffix < b_count - prefix &&
         jh_span_equal(a[a_count - 1 - suffix], b[b_count - 1 - suffix]))
    ++suffix;
  jb_vector_t script = {0};
  hal_status_t status = vector_of(&script, 1);
  if (status == HAL_OK)
    status = edit_script(a, a_count, b, b_count, prefix, suffix, &script);
  if (status == HAL_OK && prefix != 0)
    status = add_opcode(codes, op_equal, prefix, prefix);
  const unsigned char *tags = jb_vector_const_data(&script);
  for (size_t index = 0; status == HAL_OK && index < jb_vector_size(&script);
       ++index)
    status = add_opcode(codes, tags[index], tags[index] == op_insert ? 0 : 1,
                        tags[index] == op_remove ? 0 : 1);
  if (status == HAL_OK && suffix != 0)
    status = add_opcode(codes, op_equal, suffix, suffix);
  jb_vector_destroy(&script);
  return status;
}

/* A hunk range as difflib writes it. */
static void format_range(char *buffer, size_t capacity, size_t start,
                         size_t stop) {
  size_t beginning = start + 1;
  const size_t length = stop - start;
  if (length == 1) {
    (void)snprintf(buffer, capacity, "%zu", beginning);
    return;
  }
  if (length == 0)
    --beginning;
  (void)snprintf(buffer, capacity, "%zu,%zu", beginning, length);
}

/* Header and lines of the hunk codes[begin, end). */
static hal_status_t write_hunk(const opcode_t *codes, size_t begin, size_t end,
                               const jb_json_span_t *from_lines,
                               const jb_json_span_t *to_lines,
                               jh_diff_line_fn line, void *user) {
  char from_range[48], to_range[48], header[112];
  format_range(from_range, sizeof from_range, codes[begin].a_begin,
               codes[end - 1].a_end);
  format_range(to_range, sizeof to_range, codes[begin].b_begin,
               codes[end - 1].b_end);
  (void)snprintf(header, sizeof header, "@@ -%s +%s @@", from_range, to_range);
  const jb_json_span_t none = {"", 0};
  hal_status_t status = line(user, jh_span(header), none);
  for (size_t index = begin; status == HAL_OK && index < end; ++index) {
    const opcode_t *code = &codes[index];
    if (code->tag == op_insert) {
      for (size_t j = code->b_begin; status == HAL_OK && j < code->b_end; ++j)
        status = line(user, jh_span("+"), to_lines[j]);
      continue;
    }
    const jb_json_span_t mark = jh_span(code->tag == op_equal ? " " : "-");
    for (size_t i = code->a_begin; status == HAL_OK && i < code->a_end; ++i)
      status = line(user, mark, from_lines[i]);
  }
  return status;
}

/* Split codes into hunks: an equal run longer than twice the context ends one
 * hunk with its first lines and starts the next with its last ones. hunks
 * receives the trimmed runs, ends the end index of each hunk in hunks. */
static hal_status_t group_hunks(jb_vector_t *codes, size_t context,
                                jb_vector_t *hunks, jb_vector_t *ends) {
  const size_t count = jb_vector_size(codes);
  opcode_t *first = jb_vector_at(codes, 0);
  opcode_t *last = jb_vector_at(codes, count - 1);
  if (first->tag == op_equal) {
    first->a_begin =
        larger(first->a_begin, first->a_end - smaller(first->a_end, context));
    first->b_begin =
        larger(first->b_begin, first->b_end - smaller(first->b_end, context));
  }
  if (last->tag == op_equal) {
    last->a_end = smaller(last->a_end, last->a_begin + context);
    last->b_end = smaller(last->b_end, last->b_begin + context);
  }
  hal_status_t status = HAL_OK;
  for (size_t index = 0; status == HAL_OK && index < count; ++index) {
    opcode_t code = *(const opcode_t *)jb_vector_const_at(codes, index);
    if (code.tag == op_equal && code.a_end - code.a_begin > 2 * context) {
      const opcode_t head = {
          code.tag, code.a_begin, smaller(code.a_end, code.a_begin + context),
          code.b_begin, smaller(code.b_end, code.b_begin + context)};
      status = jb_vector_append(hunks, &head, 1);
      const size_t end = jb_vector_size(hunks);
      if (status == HAL_OK)
        status = jb_vector_append(ends, &end, 1);
      code.a_begin = larger(code.a_begin, code.a_end - context);
      code.b_begin = larger(code.b_begin, code.b_end - context);
    }
    if (status == HAL_OK)
      status = jb_vector_append(hunks, &code, 1);
  }
  if (status != HAL_OK)
    return status;
  const size_t closed = jb_vector_size(ends);
  const size_t begin =
      closed == 0 ? 0 : *(const size_t *)jb_vector_const_at(ends, closed - 1);
  const size_t end = jb_vector_size(hunks);
  // The trailing group closes unless it is empty or one equal run alone.
  if (end != begin &&
      !(end - begin == 1 &&
        ((const opcode_t *)jb_vector_const_at(hunks, begin))->tag == op_equal))
    status = jb_vector_append(ends, &end, 1);
  return status;
}

hal_status_t jh_unified_diff(const jb_json_span_t *from_lines,
                             size_t from_count, const jb_json_span_t *to_lines,
                             size_t to_count, jb_json_span_t from,
                             jb_json_span_t to, jh_diff_line_fn line,
                             void *user) {
  if (line == NULL || (from_lines == NULL && from_count != 0) ||
      (to_lines == NULL && to_count != 0))
    return HAL_EINVAL;
  jb_vector_t codes = {0}, hunks = {0}, ends = {0};
  hal_status_t status = vector_of(&codes, sizeof(opcode_t));
  if (status == HAL_OK)
    status = vector_of(&hunks, sizeof(opcode_t));
  if (status == HAL_OK)
    status = vector_of(&ends, sizeof(size_t));
  if (status == HAL_OK)
    status = opcodes(from_lines, from_count, to_lines, to_count, &codes);
  if (status == HAL_OK && jb_vector_size(&codes) != 0)
    status = group_hunks(&codes, 3, &hunks, &ends);
  if (status == HAL_OK && jb_vector_size(&ends) != 0) {
    status = line(user, jh_span("--- "), from);
    if (status == HAL_OK)
      status = line(user, jh_span("+++ "), to);
    const opcode_t *runs = jb_vector_const_data(&hunks);
    const size_t *stops = jb_vector_const_data(&ends);
    size_t begin = 0;
    for (size_t index = 0; status == HAL_OK && index < jb_vector_size(&ends);
         ++index) {
      status = write_hunk(runs, begin, stops[index], from_lines, to_lines, line,
                          user);
      begin = stops[index];
    }
  }
  jb_vector_destroy(&ends);
  jb_vector_destroy(&hunks);
  jb_vector_destroy(&codes);
  return status;
}

hal_status_t jh_write_generated(const char *path, jb_json_span_t content,
                                bool *changed, jh_failure_t *failure) {
  if (path == NULL || changed == NULL ||
      (content.data == NULL && content.length != 0))
    return jh_fail_arguments(failure);
  *changed = false;
  return jh_require(
      failure,
      jb_file_write_atomic(path, content.data, content.length, false, changed),
      3, "cannot publish generated output");
}

bool jh_generated_path_relative(jb_json_span_t path) {
  if (path.data == NULL || path.length == 0 || path.data[0] == '/' ||
      path.data[0] == '\\' || (path.length > 1 && path.data[1] == ':'))
    return false;
  size_t start = 0;
  for (size_t at = 0; at <= path.length; ++at) {
    if (at < path.length && path.data[at] != '/' && path.data[at] != '\\')
      continue;
    if (at - start == 2 && path.data[start] == '.' &&
        path.data[start + 1] == '.')
      return false;
    start = at + 1;
  }
  return true;
}

hal_status_t jh_write_generated_mode(const char *path, jb_json_span_t content,
                                     uint32_t mode, bool *changed,
                                     jh_failure_t *failure) {
  if (path == NULL || changed == NULL ||
      (content.data == NULL && content.length != 0))
    return jh_fail_arguments(failure);
  *changed = false;
  return jh_require(failure,
                    jb_file_write_atomic_mode(path, content.data,
                                              content.length, false, mode,
                                              changed),
                    3, "cannot publish generated output");
}

/* Lines of text without their terminators (LF or CR LF), borrowing its
 * bytes. */
static hal_status_t split_lines(jb_json_span_t text, jb_vector_t *lines) {
  hal_status_t status = HAL_OK;
  for (size_t start = 0; status == HAL_OK && start < text.length;) {
    const char *newline = memchr(text.data + start, '\n', text.length - start);
    const size_t end =
        newline == NULL ? text.length : (size_t)(newline - text.data);
    size_t stop = end;
    if (stop > start && text.data[stop - 1] == '\r')
      --stop;
    const jb_json_span_t item = {text.data + start, stop - start};
    status = jb_vector_append(lines, &item, 1);
    start = end + 1;
  }
  return status;
}

/* Diff lines printed on stderr until the limit; then the truncation mark
 * stops the diff with HAL_ECANCELED. */
typedef struct {
  size_t written, limit;
} diff_print_t;

static hal_status_t print_line(void *user, jb_json_span_t prefix,
                               jb_json_span_t text) {
  diff_print_t *state = user;
  if (state->written == state->limit) {
    (void)fputs("... diff truncated ...\n", stderr);
    return HAL_ECANCELED;
  }
  ++state->written;
  // The text ends at its first NUL byte, as printf("%s") of a line did.
  const char *nul =
      text.length == 0 ? NULL : memchr(text.data, '\0', text.length);
  const size_t shown = nul == NULL ? text.length : (size_t)(nul - text.data);
  (void)fwrite(prefix.data, 1, prefix.length, stderr);
  if (shown != 0)
    (void)fwrite(text.data, 1, shown, stderr);
  (void)fputc('\n', stderr);
  return HAL_OK;
}

static hal_status_t print_diff(jb_json_span_t actual, jb_json_span_t content,
                               const char *path, size_t diff_limit,
                               jb_json_span_t label, jh_failure_t *failure) {
  jb_vector_t from_lines = {0}, to_lines = {0};
  hal_status_t status = vector_of(&from_lines, sizeof(jb_json_span_t));
  if (status == HAL_OK)
    status = vector_of(&to_lines, sizeof(jb_json_span_t));
  if (status == HAL_OK)
    status = split_lines(actual, &from_lines);
  if (status == HAL_OK)
    status = split_lines(content, &to_lines);
  diff_print_t printed = {0, diff_limit};
  if (status == HAL_OK)
    status = jh_unified_diff(
        jb_vector_const_data(&from_lines), jb_vector_size(&from_lines),
        jb_vector_const_data(&to_lines), jb_vector_size(&to_lines),
        jh_span(path), label, print_line, &printed);
  jb_vector_destroy(&to_lines);
  jb_vector_destroy(&from_lines);
  if (status == HAL_OK || status == HAL_ECANCELED)
    return HAL_OK;
  return jh_fail_storage(failure, status);
}

hal_status_t jh_check_generated(const char *path, jb_json_span_t content,
                                size_t diff_limit, jb_json_span_t label,
                                bool *current, jh_failure_t *failure) {
  if (path == NULL || current == NULL ||
      (content.data == NULL && content.length != 0) ||
      (label.data == NULL && label.length != 0))
    return jh_fail_arguments(failure);
  jb_file_data_t existing = {0};
  const hal_status_t read = jb_file_read(path, 64 * 1024 * 1024, &existing);
  if (read == HAL_ENOENT) {
    (void)fprintf(stderr, "error: missing generated artifact %s\n", path);
    *current = false;
    return HAL_OK;
  }
  if (read != HAL_OK)
    return jh_fail_text(failure, read,
                        read == HAL_EOVERFLOW || read == HAL_ENOMEM ? 4 : 3,
                        "cannot read generated output");
  const jb_json_span_t actual = {(const char *)existing.data, existing.length};
  *current = jh_span_equal(actual, content);
  hal_status_t status = HAL_OK;
  if (!*current) {
    (void)fprintf(stderr, "error: stale generated artifact %s\n", path);
    status = print_diff(actual, content, path, diff_limit, label, failure);
  }
  jb_file_release(&existing);
  return status;
}

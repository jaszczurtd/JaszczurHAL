#include "jh_project_config.h"
#include "jh_config_expression.h"
#include "jh_feature_registry.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <jb/file.h>
#include <jb/filesystem.h>
#include <jb/map.h>
#include <jb/utf8.h>
#include <stdio.h>
#include <string.h>

static const char header_name[] = "hal_project_config.h";
static const char variants_macro[] = "JH_PROJECT_VARIANTS";
static const char targets_macro[] = "JH_PROJECT_TARGETS";
static const char scope_code[] = "JH-CFG-SCOPE";
static const char variant_code[] = "JH-CFG-VARIANT";
static const char *const target_selectors[] = {
    "HAL_TARGET_RP2040",    "HAL_TARGET_RP2350_ARM", "HAL_TARGET_RP2350_RISCV",
    "HAL_TARGET_STM32G474", "HAL_TARGET_ESP32",      "HAL_TARGET_ESP32_S3",
    "HAL_TARGET_MOCK"};
/* As in project_config.py. */
enum { MAX_INCLUDE_DEPTH = 16 };
/* Limits of jh where Python has none or stops on its recursion limit. */
enum { MAX_MACRO_NESTING = 1000, MAX_FILE_BYTES = 16 * 1024 * 1024 };

/* The functions below return the status of a failed jhbase container as it
 * is and record every other failure; the public functions turn the former
 * into a record without a message (finish). */

static const jb_allocator_t *heap(void) { return jb_heap_allocator(); }

static hal_status_t text_init(jb_string_t *text) {
  return jb_string_init(text, heap());
}

static hal_status_t text_set(jb_string_t *text, jb_json_span_t value) {
  return jb_string_assign(text, value.data, value.length);
}

/* Start the zero string text with a copy of value. */
static hal_status_t text_new(jb_string_t *text, jb_json_span_t value) {
  const hal_status_t status = text_init(text);
  return status == HAL_OK ? text_set(text, value) : status;
}

static void put_size(jb_string_t *out, hal_status_t *status, size_t value) {
  char digits[24];
  const int written = snprintf(digits, sizeof digits, "%zu", value);
  if (*status == HAL_OK)
    *status = jb_string_append(out, digits, (size_t)written);
}

static void failure_reset(jh_failure_t *failure) {
  jb_string_clear(&failure->message);
  jb_string_clear(&failure->code);
  jb_string_clear(&failure->source);
  failure->status = HAL_OK;
  failure->exit_code = 0;
}

static hal_status_t finish(jh_failure_t *failure, hal_status_t status) {
  return status != HAL_OK && failure->status == HAL_OK
             ? jh_fail_storage(failure, status)
             : status;
}

static hal_status_t reject(jh_failure_t *failure, jb_json_span_t where,
                           const char *code, const jb_json_span_t *parts,
                           size_t count) {
  return jh_fail_coded(failure, HAL_ECONFIG, 2, code, where, parts, count);
}

static hal_status_t limit(jh_failure_t *failure, jb_json_span_t where,
                          const jb_json_span_t *parts, size_t count) {
  return jh_fail_coded(failure, HAL_EOVERFLOW, 4, scope_code, where, parts,
                       count);
}

/* A line of a file: where() of the Python reader is "path:number". */
typedef struct {
  jb_json_span_t path;
  size_t number;
} place_t;

static hal_status_t place_text(jb_string_t *out, place_t place) {
  hal_status_t status = HAL_OK;
  jb_string_clear(out);
  jh_put_span(out, &status, place.path);
  jh_put(out, &status, ":");
  put_size(out, &status, place.number);
  return status;
}

static hal_status_t reject_at(jh_failure_t *failure, place_t place,
                              const char *code, const jb_json_span_t *parts,
                              size_t count) {
  jb_string_t where = {0};
  hal_status_t status = text_init(&where);
  if (status == HAL_OK)
    status = place_text(&where, place);
  if (status == HAL_OK)
    status = reject(failure, jh_string_span(&where), code, parts, count);
  jb_string_destroy(&where);
  return status;
}

/* Tokens a read, or one query on its result, may still visit. */
typedef struct {
  size_t *remaining;
  size_t total;
} work_budget_t;

static hal_status_t charge(work_budget_t *budget, jb_json_span_t where,
                           size_t amount, jh_failure_t *failure) {
  if (amount > *budget->remaining) {
    jb_string_t total = {0};
    hal_status_t status = text_init(&total);
    put_size(&total, &status, budget->total);
    if (status == HAL_OK)
      status =
          limit(failure, where,
                JH_PARTS(jh_span("expansion and dependency tracking exceed "),
                         jh_string_span(&total), jh_span(" tokens")));
    jb_string_destroy(&total);
    return status;
  }
  *budget->remaining -= amount;
  return HAL_OK;
}

/* ── Text ──────────────────────────────────────────────────────────────────
 * Files are valid UTF-8; whitespace is str.isspace() of Python, everything
 * else the reader classifies is ASCII. */

static jb_json_span_t sub(jb_json_span_t text, size_t from, size_t to) {
  const jb_json_span_t part = {text.data + from, to - from};
  return part;
}

static size_t scalar_length(jb_json_span_t text, size_t offset) {
  size_t used = 1;
  (void)jh_config_scalar_at(text, offset, &used);
  return used;
}

static jb_json_span_t strip(jb_json_span_t text) {
  return jh_config_rstrip(
      sub(text, jh_config_skip_space(text, 0), text.length));
}

static bool equals(jb_json_span_t text, const char *expected) {
  return jh_span_equal(text, jh_span(expected));
}

/* equals() for a string literal, without measuring it at run time. */
#define IS(text, literal) span_is((text), "" literal, sizeof(literal) - 1)
static bool span_is(jb_json_span_t text, const char *expected, size_t length) {
  return text.length == length && memcmp(text.data, expected, length) == 0;
}

static size_t find_byte(jb_json_span_t text, char byte, size_t from) {
  const char *found = from < text.length
                          ? memchr(text.data + from, byte, text.length - from)
                          : NULL;
  return found != NULL ? (size_t)(found - text.data) : text.length;
}

static bool is_target_selector(jb_json_span_t name) {
  for (size_t i = 0; i < COUNTOF(target_selectors); ++i)
    if (equals(name, target_selectors[i]))
      return true;
  return false;
}

/* ── Sets of names ─────────────────────────────────────────────────────────
 * A set the reader builds and asks is an ordered map: adding and finding a
 * name costs the logarithm of the size of the set, whatever the order the
 * names come in. The results hold a set as a list: jb_string_t elements in
 * code point order. */

static int span_order(const void *left, const void *right, void *context) {
  (void)context;
  return jh_span_compare(*(const jb_json_span_t *)left,
                         *(const jb_json_span_t *)right);
}

static int index_order(const void *left, const void *right, void *context) {
  const size_t first = *(const size_t *)left, second = *(const size_t *)right;
  (void)context;
  return first < second ? -1 : first > second ? 1 : 0;
}

/* Start a map from names to values of size bytes. Its keys borrow their
 * bytes from what the values name. */
static hal_status_t name_map_init(jb_map_t *map, size_t size) {
  return jb_map_init(map, sizeof(jb_json_span_t), size, span_order, NULL,
                     heap());
}

typedef struct {
  jb_map_t order;    /* each name and its index in owned */
  jb_vector_t owned; /* jb_string_t elements: the bytes the keys borrow */
} names_t;

static hal_status_t names_init(names_t *names) {
  *names = (names_t){0};
  const hal_status_t status = name_map_init(&names->order, sizeof(size_t));
  return status == HAL_OK ? jh_list_init(&names->owned) : status;
}

/* Release a set; a zero set is harmless. */
static void names_destroy(names_t *names) {
  jb_map_destroy(&names->order);
  jh_list_destroy(&names->owned);
}

static size_t names_size(const names_t *names) {
  return jb_map_size(&names->order);
}

static bool names_contain(const names_t *names, jb_json_span_t name) {
  const void *index = NULL;
  return jb_map_find_const(&names->order, &name, &index) == HAL_OK;
}

static hal_status_t names_add(names_t *names, jb_json_span_t name,
                              bool *added) {
  if (added != NULL)
    *added = false;
  if (names_contain(names, name))
    return HAL_OK;
  const size_t index = jb_vector_size(&names->owned);
  hal_status_t status = jh_list_push(&names->owned, name);
  if (status != HAL_OK)
    return status;
  const jb_json_span_t key = jh_string_span(jh_list_at(&names->owned, index));
  status = jb_map_insert(&names->order, &key, &index);
  if (status != HAL_OK) {
    jb_string_destroy(jb_vector_at(&names->owned, index));
    (void)jb_vector_resize(&names->owned, index);
    return status;
  }
  if (added != NULL)
    *added = true;
  return HAL_OK;
}

/* A walk over a set in code point order; start with a zero walk. */
typedef struct {
  jb_map_iter_t at;
  bool started;
} names_walk_t;

/* The next name of the set, borrowed from it; false after the last one. */
static bool names_next(const names_t *names, names_walk_t *walk,
                       jb_json_span_t *name) {
  const hal_status_t status = walk->started
                                  ? jb_map_next(&walk->at)
                                  : jb_map_first(&names->order, &walk->at);
  const void *key = NULL, *index = NULL;
  walk->started = true;
  if (status != HAL_OK || jb_map_iter_get(&walk->at, &key, &index) != HAL_OK)
    return false;
  *name = *(const jb_json_span_t *)key;
  return true;
}

static hal_status_t names_merge(names_t *names, const names_t *other) {
  hal_status_t status = HAL_OK;
  names_walk_t walk = {0};
  jb_json_span_t name = {"", 0};
  while (status == HAL_OK && names_next(other, &walk, &name))
    status = names_add(names, name, NULL);
  return status;
}

static hal_status_t names_add_list(names_t *names, const jb_vector_t *list) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(list); ++i)
    status = names_add(names, jh_string_span(jh_list_at(list, i)), NULL);
  return status;
}

/* Append the names of the set to list in code point order. */
static hal_status_t names_to_list(const names_t *names, jb_vector_t *list) {
  hal_status_t status = HAL_OK;
  names_walk_t walk = {0};
  jb_json_span_t name = {"", 0};
  while (status == HAL_OK && names_next(names, &walk, &name))
    status = jh_list_push(list, name);
  return status;
}

/* ── Macros ──────────────────────────────────────────────────────────────── */

static void macro_destroy(jh_config_macro_t *macro) {
  jb_string_destroy(&macro->name);
  jb_string_destroy(&macro->body);
  jb_string_destroy(&macro->source);
  jh_list_destroy(&macro->parameters);
}

static hal_status_t macro_init(jh_config_macro_t *macro) {
  *macro = (jh_config_macro_t){0};
  hal_status_t status = text_init(&macro->name);
  if (status == HAL_OK)
    status = text_init(&macro->body);
  if (status == HAL_OK)
    status = text_init(&macro->source);
  if (status == HAL_OK)
    status = jh_list_init(&macro->parameters);
  return status;
}

static const jh_config_macro_t *macro_at(const jh_project_config_t *config,
                                         size_t index) {
  return jb_vector_const_at(&config->macros, index);
}

/* The place of the macro name defines in the list of macros. */
static bool macro_index(const jh_project_config_t *config, jb_json_span_t name,
                        size_t *index) {
  const void *found = NULL;
  if (jb_map_find_const(&config->macro_names, &name, &found) != HAL_OK)
    return false;
  *index = *(const size_t *)found;
  return true;
}

const jh_config_macro_t *
jh_project_config_find(const jh_project_config_t *config, jb_json_span_t name) {
  size_t index = 0;
  return config != NULL && macro_index(config, name, &index)
             ? macro_at(config, index)
             : NULL;
}

/* Define value, which the table takes over on success. */
static hal_status_t macro_set(jh_project_config_t *config,
                              jh_config_macro_t *value) {
  size_t index = 0;
  if (macro_index(config, jh_string_span(&value->name), &index)) {
    // The index borrows the name of the first definition, which stays.
    jh_config_macro_t *existing = jb_vector_at(&config->macros, index);
    const jb_string_t name = existing->name;
    existing->name = (jb_string_t){0};
    macro_destroy(existing);
    jb_string_destroy(&value->name);
    *existing = *value;
    existing->name = name;
    *value = (jh_config_macro_t){0};
    return HAL_OK;
  }
  index = jb_vector_size(&config->macros);
  hal_status_t status = jb_vector_append(&config->macros, value, 1);
  if (status != HAL_OK)
    return status;
  const jb_json_span_t key = jh_string_span(&value->name);
  status = jb_map_insert(&config->macro_names, &key, &index);
  if (status != HAL_OK) {
    // The caller still owns value: drop the copied bytes only.
    (void)jb_vector_resize(&config->macros, index);
    return status;
  }
  *value = (jh_config_macro_t){0};
  return HAL_OK;
}

/* Undefine name. Its place in the list stays as a macro without a name until
 * the read ends (macros_close_gaps): the other macros keep their indexes. */
static void macro_erase(jh_project_config_t *config, jb_json_span_t name) {
  size_t index = 0;
  if (!macro_index(config, name, &index))
    return;
  // The key borrows the name: the index entry goes first.
  (void)jb_map_erase(&config->macro_names, &name);
  macro_destroy(jb_vector_at(&config->macros, index));
  ++config->macro_gaps;
}

/* Remove the places #undef left in the list of macros and point the index at
 * the new places of the macros behind them. */
static void macros_close_gaps(jh_project_config_t *config) {
  if (config->macro_gaps == 0)
    return;
  jh_config_macro_t *items = jb_vector_data(&config->macros);
  const size_t count = jb_vector_size(&config->macros);
  size_t kept = 0;
  for (size_t i = 0; i < count; ++i) {
    if (jb_string_size(&items[i].name) == 0)
      continue;
    if (kept != i) {
      const jb_json_span_t name = jh_string_span(&items[i].name);
      void *index = NULL;
      items[kept] = items[i];
      if (jb_map_find(&config->macro_names, &name, &index) == HAL_OK)
        *(size_t *)index = kept;
    }
    ++kept;
  }
  (void)jb_vector_resize(&config->macros, kept);
  config->macro_gaps = 0;
}

bool jh_config_macro_is_feature(const jh_config_macro_t *macro) {
  return jh_feature_symbol(jh_string_span(&macro->name), false) &&
         !IS(jh_string_span(&macro->source), "predefined");
}

/* ── Files ─────────────────────────────────────────────────────────────────
 */

/* Path.resolve() of Python, the name of every file the reader reads:
 * absolute, the symbolic links of existing parts resolved, the rest
 * normalized. A relative path joins base, or the working directory when base
 * is NULL. A path that cannot be resolved (a loop of links) is made absolute
 * and normalized as written; reading it then fails. */
static hal_status_t resolve_path(const char *path, const char *base,
                                 jb_string_t *out, jh_failure_t *failure) {
  hal_status_t status = jh_python_resolve(path, base, out, failure);
  // Memory and the limits of jh are no property of the path.
  if (status == HAL_OK || status == HAL_ENOMEM || status == HAL_EOVERFLOW ||
      jb_string_size(&failure->message) == 0)
    return status;
  failure_reset(failure);
  jb_string_t cwd = {0};
  status = text_init(&cwd);
  if (status == HAL_OK && base == NULL) {
    status = jh_working_directory(&cwd, failure);
    base = jb_string_data(&cwd);
  }
  if (status == HAL_OK)
    status = jh_absolute_path(path, base, out, failure);
  jb_string_destroy(&cwd);
  return status;
}

/* A path the helpers cannot work out is a rejection like every other one:
 * their message gets the code and the place where. A failure without a
 * message (memory) stays as it is. */
static hal_status_t path_failed(jh_failure_t *failure, hal_status_t status,
                                jb_json_span_t where) {
  if (status == HAL_OK || jb_string_size(&failure->message) == 0 ||
      jb_string_size(&failure->code) != 0)
    return status;
  return jh_fail_coded(failure, status, failure->exit_code, scope_code, where,
                       JH_PARTS(jh_string_span(&failure->message)));
}

/* The file path cannot be read, or the system does not tell what it is. */
static hal_status_t cannot_read(jh_failure_t *failure, hal_status_t status,
                                const char *path) {
  return jh_fail_coded(failure, status,
                       status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : 3,
                       scope_code, jh_span(path),
                       JH_PARTS(jh_span("cannot read: "),
                                jh_span(hal_status_to_string(status))));
}

/* Whether path names a regular file: *regular. Only a path that does not
 * exist names none; an answer the system cannot give is returned as it is,
 * so that a file is never taken for a missing one. */
static hal_status_t regular_file(const char *path, bool *regular) {
  jb_fs_info_t info = {0};
  *regular = false;
  if (path[0] == '\0')
    return HAL_OK;
  const hal_status_t status = jb_fs_status(path, true, &info);
  if (status == HAL_ENOENT)
    return HAL_OK;
  if (status == HAL_OK)
    *regular = info.kind == JB_FS_REGULAR;
  return status;
}

/* regular_file() for a file the caller names: one that cannot be inspected
 * fails as one that cannot be read. */
static hal_status_t named_file(const char *path, bool *regular,
                               jh_failure_t *failure) {
  const hal_status_t status = regular_file(path, regular);
  return status == HAL_OK ? HAL_OK : cannot_read(failure, status, path);
}

/* The bytes of path as valid UTF-8 text. */
static hal_status_t read_text(const char *path, jb_file_data_t *data,
                              jh_failure_t *failure) {
  const hal_status_t status = jb_file_read(path, MAX_FILE_BYTES, data);
  if (status != HAL_OK)
    return cannot_read(failure, status, path);
  size_t bad = 0;
  if (jb_utf8_validate((const char *)data->data, data->length, &bad) !=
      HAL_OK) {
    jb_string_t offset = {0};
    hal_status_t stored = text_init(&offset);
    put_size(&offset, &stored, bad);
    if (stored == HAL_OK)
      stored = jh_fail_coded(failure, HAL_EPROTO, 2, scope_code, jh_span(path),
                             JH_PARTS(jh_span("is not valid UTF-8 at byte "),
                                      jh_string_span(&offset)));
    jb_string_destroy(&offset);
    return stored;
  }
  return HAL_OK;
}

static jb_json_span_t file_span(const jb_file_data_t *data) {
  const jb_json_span_t text = {(const char *)data->data, data->length};
  return text;
}

/* ── Logical lines ───────────────────────────────────────────────────────── */

/* One line that may hold a directive: its source line and its place in the
 * text of the lines. */
typedef struct {
  size_t number, offset, length;
} logical_line_t;

/* The comment-free lines of a file that start with "#": text holds the bytes
 * of all of them. A file costs what its directives take, whatever else it
 * holds. */
typedef struct {
  jb_string_t text;
  jb_vector_t items; /* logical_line_t */
} lines_t;

static hal_status_t lines_init(lines_t *lines) {
  *lines = (lines_t){0};
  const hal_status_t status = text_init(&lines->text);
  return status == HAL_OK
             ? jb_vector_init(&lines->items, sizeof(logical_line_t), heap())
             : status;
}

/* Release the lines; zero lines are harmless. */
static void lines_destroy(lines_t *lines) {
  jb_vector_destroy(&lines->items);
  jb_string_destroy(&lines->text);
}

static size_t lines_count(const lines_t *lines) {
  return jb_vector_size(&lines->items);
}

/* The text of line index, valid until the lines are destroyed, and the
 * number of its source line. */
static jb_json_span_t line_text(const lines_t *lines, size_t index,
                                size_t *number) {
  const logical_line_t *line = jb_vector_const_at(&lines->items, index);
  const jb_json_span_t text = {jb_string_data(&lines->text) + line->offset,
                               line->length};
  *number = line->number;
  return text;
}

/* End the line that starts at start in the text: it stays when "#" is the
 * first thing in it that is no whitespace, and its bytes go otherwise. */
static hal_status_t line_push(lines_t *lines, size_t number, size_t start) {
  const size_t length = jb_string_size(&lines->text) - start;
  if (length == 0)
    return HAL_OK;
  const jb_json_span_t line = {jb_string_data(&lines->text) + start, length};
  const size_t first = jh_config_skip_space(line, 0);
  if (first >= length || line.data[first] != '#')
    return jb_string_resize(&lines->text, start, '\0');
  const logical_line_t item = {number, start, length};
  return jb_vector_append(&lines->items, &item, 1);
}

/* The lines of input that may hold a directive, with their source line,
 * split exactly as preprocessor_logical_lines() of the Python reader splits
 * the lines of a file. result is initialized and empty. */
static hal_status_t logical_lines(jb_json_span_t input, lines_t *result) {
  jb_string_t text = {0};
  jb_vector_t numbers = {0};
  hal_status_t status = text_init(&text);
  if (status == HAL_OK)
    status = jb_vector_init(&numbers, sizeof(size_t), heap());
  if (status == HAL_OK)
    status = jb_string_resize(&text, input.length, '\0');
  if (status == HAL_OK)
    status = jb_vector_resize(&numbers, input.length);
  // Universal newlines and backslash-newline splices; each remaining byte
  // keeps the number of its source line.
  char *bytes = jb_string_mutable_data(&text);
  size_t *numbered = jb_vector_data(&numbers);
  size_t line = 1, kept = 0;
  for (size_t i = 0; status == HAL_OK && i < input.length;) {
    char c = input.data[i];
    size_t used = 1;
    if (c == '\r') {
      c = '\n';
      if (i + 1 < input.length && input.data[i + 1] == '\n')
        used = 2;
    }
    if (c == '\\' && i + 1 < input.length &&
        (input.data[i + 1] == '\n' || input.data[i + 1] == '\r')) {
      ++line;
      i += input.data[i + 1] == '\r' && i + 2 < input.length &&
                   input.data[i + 2] == '\n'
               ? 3
               : 2;
      continue;
    }
    bytes[kept] = c;
    numbered[kept++] = line;
    if (c == '\n')
      ++line;
    i += used;
  }
  if (status == HAL_OK)
    status = jb_string_resize(&text, kept, '\0');
  const jb_json_span_t spliced = jh_string_span(&text);
  const size_t *line_of = jb_vector_const_data(&numbers);
  // start is where the line being built begins in the text of result.
  size_t origin = 0, offset = 0, start = 0;
  char quote = 0;
  while (status == HAL_OK && offset < spliced.length) {
    const char c = spliced.data[offset];
    size_t used = scalar_length(spliced, offset), ignored = 0;
    // Bytes to append at offset, and whether the line ends there.
    size_t length = used;
    bool emit = false;
    if (quote != 0) {
      if (c == '\\' && offset + 1 < spliced.length) {
        length += scalar_length(spliced, offset + 1);
      } else if (c == quote) {
        quote = 0;
      } else if (c == '\n') {
        emit = true;
        quote = 0;
      }
      used = length;
    } else {
      const char next =
          offset + 1 < spliced.length ? spliced.data[offset + 1] : 0;
      if (c == '/' && (next == '/' || next == '*')) {
        status = jb_string_append(&result->text, " ", 1);
        if (next == '/') {
          offset = find_byte(spliced, '\n', offset + 2);
        } else {
          size_t end = offset + 2;
          while (end + 1 < spliced.length &&
                 !(spliced.data[end] == '*' && spliced.data[end + 1] == '/'))
            ++end;
          offset = end + 1 < spliced.length ? end + 2 : spliced.length;
        }
        continue;
      }
      if (c == '"' || c == '\'') {
        quote = c;
        used = length = 1;
      } else if (c == '\n') {
        length = 0;
        emit = true;
        used = 1;
      }
    }
    // A backslash in a literal appends two scalars, each of which may be
    // the first one of the line that is no whitespace.
    for (size_t at = offset; status == HAL_OK && at < offset + length;) {
      const size_t step = scalar_length(spliced, at);
      if (origin == 0 && !jh_config_space_at(spliced, at, &ignored))
        origin = line_of[at];
      status = jb_string_append(&result->text, spliced.data + at, step);
      at += step;
    }
    if (status == HAL_OK && emit) {
      status = line_push(result, origin != 0 ? origin : line_of[offset], start);
      start = jb_string_size(&result->text);
      origin = 0;
    }
    offset += used;
  }
  if (status == HAL_OK)
    status = line_push(
        result,
        origin != 0 ? origin
                    : (spliced.length == 0 ? 1 : line_of[spliced.length - 1]),
        start);
  jb_vector_destroy(&numbers);
  jb_string_destroy(&text);
  return status;
}

/* ^\s*#\s*([A-Za-z_]+)\b(.*)$ of the Python reader. A name followed by an
 * ASCII digit is no directive there; one followed by another non-ASCII
 * scalar than whitespace depends on Unicode word classes, which jh rejects.
 * name and argument borrow from text. */
static hal_status_t split_directive(jb_json_span_t text, place_t place,
                                    bool *found, jb_json_span_t *name,
                                    jb_json_span_t *argument,
                                    jh_failure_t *failure) {
  *found = false;
  size_t i = jh_config_skip_space(text, 0);
  if (i >= text.length || text.data[i] != '#')
    return HAL_OK;
  i = jh_config_skip_space(text, i + 1);
  const size_t start = i;
  while (i < text.length && jh_config_name_start(text.data[i]))
    ++i;
  if (i == start)
    return HAL_OK;
  if (i < text.length) {
    size_t used = 0;
    if (jh_ascii_is_digit(text.data[i]))
      return HAL_OK;
    if ((unsigned char)text.data[i] >= 0x80 &&
        !jh_config_space_at(text, i, &used))
      return reject_at(failure, place, scope_code,
                       JH_PARTS(jh_span("non-ASCII character after #"),
                                sub(text, start, i)));
  }
  *name = sub(text, start, i);
  *argument = strip(sub(text, i, text.length));
  *found = true;
  return HAL_OK;
}

/* ── Expressions ─────────────────────────────────────────────────────────── */

/* The failure of the tokenizer or the evaluator, which holds details only,
 * as a rejection at source. */
static hal_status_t expression_failed(jh_failure_t *failure,
                                      hal_status_t status,
                                      jb_json_span_t source) {
  if (jb_string_size(&failure->message) == 0)
    return jh_fail_storage(failure, status);
  return jh_fail_coded(failure, status, failure->exit_code, scope_code, source,
                       JH_PARTS(jh_string_span(&failure->message)));
}

/* The tokens of a condition or a macro body: jb_string_t elements. */
static hal_status_t condition_tokens(jb_json_span_t input,
                                     jb_json_span_t source, jb_vector_t *tokens,
                                     jh_failure_t *failure) {
  const hal_status_t status = jh_config_tokenize(input, tokens, failure);
  return status == HAL_OK ? HAL_OK : expression_failed(failure, status, source);
}

/* One token of a condition or a macro body with what the expansion needs of
 * it, found once: the tokens of a body are visited on every path to it. */
typedef struct {
  jb_json_span_t text;
  size_t macro; /* the index of the macro text names, or NO_MACRO */
  bool defined; /* the operator defined */
} token_t;

#define NO_MACRO SIZE_MAX

/* The tokens of text at source: strings owns the bytes tokens borrows. */
typedef struct {
  jb_vector_t strings, tokens;
} token_list_t;

static void tokens_destroy(token_list_t *list) {
  jb_vector_destroy(&list->tokens);
  jh_list_destroy(&list->strings);
}

static hal_status_t tokens_of(const jh_project_config_t *config,
                              jb_json_span_t text, jb_json_span_t source,
                              token_list_t *list, jh_failure_t *failure) {
  *list = (token_list_t){0};
  hal_status_t status = jh_list_init(&list->strings);
  if (status == HAL_OK)
    status = jb_vector_init(&list->tokens, sizeof(token_t), heap());
  if (status == HAL_OK)
    status = condition_tokens(text, source, &list->strings, failure);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&list->strings);
       ++i) {
    token_t token = {jh_string_span(jh_list_at(&list->strings, i)), NO_MACRO,
                     false};
    token.defined = IS(token.text, "defined");
    if (!token.defined && !macro_index(config, token.text, &token.macro))
      token.macro = NO_MACRO;
    status = jb_vector_append(&list->tokens, &token, 1);
  }
  return status;
}

/* The operand of defined at *index, which moves past it. */
static hal_status_t defined_operand(const token_t *tokens, size_t count,
                                    size_t *index, jb_json_span_t source,
                                    jb_json_span_t *name,
                                    jh_failure_t *failure) {
  size_t at = *index;
  const bool parenthesized = at < count && IS(tokens[at].text, "(");
  if (parenthesized)
    ++at;
  bool valid = at < count && jh_config_identifier(tokens[at].text);
  if (valid) {
    *name = tokens[at++].text;
    if (parenthesized)
      valid = at < count && IS(tokens[at++].text, ")");
  }
  if (!valid)
    return reject(failure, source, scope_code,
                  JH_PARTS(jh_span("malformed defined()")));
  *index = at;
  return HAL_OK;
}

typedef struct {
  const jh_project_config_t *config;
  jb_json_span_t source;
  work_budget_t *budget;
  jh_failure_t *failure;
  /* Indexes of the macros being expanded. */
  jb_vector_t hidden;
  /* jb_json_span_t elements borrowing from the expanded token lists. */
  jb_vector_t out;
  /* The tokens of the bodies this expansion entered: token_list_t by the
   * index of the macro, made at its first expansion. An expansion costs what
   * it visits, whatever the number of macros. */
  jb_map_t bodies;
} expansion_t;

static bool hidden_macro(const expansion_t *expansion, size_t index) {
  const size_t *hidden = jb_vector_const_data(&expansion->hidden);
  const size_t count = jb_vector_size(&expansion->hidden);
  for (size_t i = 0; i < count; ++i)
    if (hidden[i] == index)
      return true;
  return false;
}

static hal_status_t expansion_body(expansion_t *expansion, size_t index,
                                   const token_list_t **body) {
  void *found = NULL;
  if (jb_map_find(&expansion->bodies, &index, &found) != HAL_OK) {
    const jh_config_macro_t *macro = macro_at(expansion->config, index);
    token_list_t list = {0};
    hal_status_t status =
        tokens_of(expansion->config, jh_string_span(&macro->body),
                  jh_string_span(&macro->source), &list, expansion->failure);
    if (status == HAL_OK)
      status = jb_map_insert(&expansion->bodies, &index, &list);
    if (status == HAL_OK)
      status = jb_map_find(&expansion->bodies, &index, &found);
    if (status != HAL_OK) {
      tokens_destroy(&list);
      return status;
    }
  }
  *body = found;
  return HAL_OK;
}

/* Release the token lists of the bodies and the map. */
static void bodies_destroy(jb_map_t *bodies) {
  jb_map_iter_t first = {0};
  while (jb_map_first(bodies, &first) == HAL_OK) {
    const void *key = NULL, *value = NULL;
    void *list = NULL;
    if (jb_map_iter_get(&first, &key, &value) != HAL_OK)
      break;
    const size_t index = *(const size_t *)key;
    if (jb_map_find(bodies, &index, &list) == HAL_OK)
      tokens_destroy(list);
    (void)jb_map_erase(bodies, &index);
  }
  jb_map_destroy(bodies);
}

/* Replace defined operators and object-like macros, C-style. */
static hal_status_t expand(expansion_t *expansion, const token_t *tokens,
                           size_t count) {
  if (jb_vector_size(&expansion->hidden) > MAX_MACRO_NESTING)
    return limit(expansion->failure, expansion->source,
                 JH_PARTS(jh_span("macros nest more than 1000 levels")));
  hal_status_t status = HAL_OK;
  for (size_t index = 0; status == HAL_OK && index < count;) {
    status =
        charge(expansion->budget, expansion->source, 1, expansion->failure);
    if (status != HAL_OK)
      break;
    const token_t *token = &tokens[index];
    if (token->defined) {
      jb_json_span_t name = {"", 0};
      ++index;
      status = defined_operand(tokens, count, &index, expansion->source, &name,
                               expansion->failure);
      if (status == HAL_OK) {
        const jb_json_span_t digit = jh_span(
            jh_project_config_find(expansion->config, name) != NULL ? "1"
                                                                    : "0");
        status = jb_vector_append(&expansion->out, &digit, 1);
      }
      continue;
    }
    if (token->macro != NO_MACRO && !hidden_macro(expansion, token->macro)) {
      if (macro_at(expansion->config, token->macro)->function_like)
        return reject(expansion->failure, expansion->source, scope_code,
                      JH_PARTS(jh_span("function-like macro "), token->text,
                               jh_span(" is not supported in conditions")));
      const token_list_t *body = NULL;
      status = expansion_body(expansion, token->macro, &body);
      if (status == HAL_OK)
        status = jb_vector_append(&expansion->hidden, &token->macro, 1);
      if (status == HAL_OK) {
        status = expand(expansion, jb_vector_const_data(&body->tokens),
                        jb_vector_size(&body->tokens));
        (void)jb_vector_resize(&expansion->hidden,
                               jb_vector_size(&expansion->hidden) - 1);
      }
    } else {
      status = jb_vector_append(&expansion->out, &token->text, 1);
    }
    ++index;
  }
  return status;
}

/* Evaluate an #if expression or a macro body. A value outside int64_t sets
 * *range and leaves its expression as the message of failure: the caller
 * decides what it means. */
static hal_status_t evaluate(const jh_project_config_t *config,
                             jb_json_span_t text, jb_json_span_t source,
                             work_budget_t *budget, int64_t *value, bool *range,
                             jh_failure_t *failure) {
  expansion_t expansion = {config, source, budget, failure, {0}, {0}, {0}};
  token_list_t tokens = {0};
  *range = false;
  hal_status_t status = tokens_of(config, text, source, &tokens, failure);
  if (status == HAL_OK)
    status = jb_vector_init(&expansion.hidden, sizeof(size_t), heap());
  if (status == HAL_OK)
    status = jb_vector_init(&expansion.out, sizeof(jb_json_span_t), heap());
  if (status == HAL_OK)
    status = jb_map_init(&expansion.bodies, sizeof(size_t),
                         sizeof(token_list_t), index_order, NULL, heap());
  if (status == HAL_OK)
    status = expand(&expansion, jb_vector_const_data(&tokens.tokens),
                    jb_vector_size(&tokens.tokens));
  if (status == HAL_OK) {
    status = jh_config_expression(jb_vector_const_data(&expansion.out),
                                  jb_vector_size(&expansion.out), value, range,
                                  failure);
    if (status != HAL_OK && jb_string_size(&failure->message) == 0)
      *range = false;
    if (status != HAL_OK && !*range)
      status = expression_failed(failure, status, source);
  }
  bodies_destroy(&expansion.bodies);
  jb_vector_destroy(&expansion.out);
  jb_vector_destroy(&expansion.hidden);
  tokens_destroy(&tokens);
  return status;
}

/* Names and punctuators of a directive argument or a macro body, as _PP_TOKEN
 * splits them: numbers (1u, 0x10) and literals are not names. names receives
 * jb_json_span_t elements borrowing from text. */
static hal_status_t pp_names(jb_json_span_t text, jb_vector_t *names) {
  const size_t size = text.length;
  const char *bytes = text.data;
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < size;) {
    const char c = bytes[i];
    const char next = i + 1 < size ? bytes[i + 1] : 0;
    if (jh_ascii_is_digit(c) || (c == '.' && jh_ascii_is_digit(next))) {
      size_t j = c == '.' ? i + 2 : i + 1;
      while (j < size) {
        const char d = bytes[j];
        if ((d == 'e' || d == 'E' || d == 'p' || d == 'P') && j + 1 < size &&
            (bytes[j + 1] == '+' || bytes[j + 1] == '-'))
          j += 2;
        else if (jh_config_name_part(d) || d == '.')
          ++j;
        else
          break;
      }
      i = j;
      continue;
    }
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < size && bytes[j] != c)
        j += bytes[j] != '\\' ? scalar_length(text, j)
             : j + 1 < size   ? 1 + scalar_length(text, j + 1)
                              : size - j;
      if (j < size) {
        i = j + 1;
        continue;
      }
    }
    size_t used = 0;
    jb_json_span_t name = {"", 0};
    if (jh_config_name_start(c)) {
      size_t j = i + 1;
      while (j < size && jh_config_name_part(bytes[j]))
        ++j;
      name = sub(text, i, j);
    } else if (jh_config_space_at(text, i, &used)) {
      i += used;
      continue;
    } else {
      name = sub(text, i, i + used);
    }
    status = jb_vector_append(names, &name, 1);
    i += name.length;
  }
  return status;
}

/* Add every name the text start uses to found: the operand of defined, and
 * each object-like macro with the names its body expands to. entered names
 * the macro not to expand (the one whose value is asked), or is empty. Each
 * macro body is scanned once: the Python reader enters a macro again on
 * every path to it, which only repeats names it already found, at a cost
 * that can grow exponentially with the depth of the macros. */
static hal_status_t
referenced_names(const jh_project_config_t *config, jb_json_span_t start,
                 jb_json_span_t entered, names_t *found, jb_json_span_t where,
                 work_budget_t *budget, jh_failure_t *failure) {
  // pending holds the macro bodies still to scan: jb_json_span_t elements.
  jb_vector_t pending = {0}, tokens = {0};
  names_t seen = {0};
  hal_status_t status =
      jb_vector_init(&pending, sizeof(jb_json_span_t), heap());
  if (status == HAL_OK)
    status = names_init(&seen);
  if (status == HAL_OK && entered.length != 0)
    status = names_add(&seen, entered, NULL);
  jb_json_span_t text = start;
  bool more = status == HAL_OK;
  while (more) {
    // Each list is tokenized when its turn comes; the order of the lists
    // does not change what is found, only when the budget runs out.
    status = jb_vector_init(&tokens, sizeof(jb_json_span_t), heap());
    if (status == HAL_OK)
      status = pp_names(text, &tokens);
    const jb_json_span_t *items = jb_vector_const_data(&tokens);
    const size_t count = jb_vector_size(&tokens);
    if (status == HAL_OK)
      status = charge(budget, where, count, failure);
    for (size_t index = 0; status == HAL_OK && index < count; ++index) {
      const jb_json_span_t token = items[index];
      if (IS(token, "defined")) {
        ++index;
        if (index < count && IS(items[index], "("))
          ++index;
        if (index < count && jh_config_identifier(items[index]))
          status = names_add(found, items[index], NULL);
        continue;
      }
      if (!jh_config_identifier(token))
        continue;
      status = names_add(found, token, NULL);
      const jh_config_macro_t *definition =
          jh_project_config_find(config, token);
      bool added = false;
      if (status == HAL_OK && definition != NULL && !definition->function_like)
        status = names_add(&seen, token, &added);
      if (status == HAL_OK && added) {
        const jb_json_span_t body = jh_string_span(&definition->body);
        status = jb_vector_append(&pending, &body, 1);
      }
    }
    jb_vector_destroy(&tokens);
    more = status == HAL_OK && jb_vector_size(&pending) != 0;
    if (more) {
      const size_t last = jb_vector_size(&pending) - 1;
      text = *(const jb_json_span_t *)jb_vector_const_at(&pending, last);
      (void)jb_vector_resize(&pending, last);
    }
  }
  names_destroy(&seen);
  jb_vector_destroy(&pending);
  return status;
}

/* ── Declarations ────────────────────────────────────────────────────────── */

/* Split a macro argument list on top-level commas: jb_json_span_t elements
 * borrowing from text, each without the whitespace around it. */
static hal_status_t split_arguments(jb_json_span_t text,
                                    jb_vector_t *arguments) {
  int depth = 0;
  char quote = 0;
  size_t start = 0;
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < text.length; ++i) {
    const char c = text.data[i];
    if (quote != 0) {
      if (c == '\\' && i + 1 < text.length)
        ++i;
      else if (c == quote)
        quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    } else if (c == ',' && depth == 0) {
      const jb_json_span_t argument = strip(sub(text, start, i));
      status = jb_vector_append(arguments, &argument, 1);
      start = i + 1;
    }
  }
  if (status == HAL_OK) {
    const jb_json_span_t argument = strip(sub(text, start, text.length));
    status = jb_vector_append(arguments, &argument, 1);
  }
  return status;
}

static jb_json_span_t callback_of(const jh_config_macro_t *declaration) {
  return jh_string_span(jh_list_at(&declaration->parameters, 0));
}

/* The argument text of the X(...) entry of a declaration X-macro at
 * *position, which moves past it; *done after the last one. One entry at a
 * time: a bad entry fails before a later malformed one, as the generator of
 * the Python reader does. */
static hal_status_t x_entry(const jh_config_macro_t *declaration,
                            size_t *position, jb_json_span_t *entry, bool *done,
                            jh_failure_t *failure) {
  const jb_json_span_t callback = callback_of(declaration);
  const jb_json_span_t body = jh_string_span(&declaration->body);
  const jb_json_span_t source = jh_string_span(&declaration->source);
  *done = *position >= jh_config_rstrip(body).length;
  if (*done)
    return HAL_OK;
  size_t i = jh_config_skip_space(body, *position);
  const bool named = body.length - i >= callback.length &&
                     jh_span_equal(sub(body, i, i + callback.length), callback);
  if (named)
    i = jh_config_skip_space(body, i + callback.length);
  if (!named || i >= body.length || body.data[i] != '(')
    return reject(failure, source, variant_code,
                  JH_PARTS(jh_string_span(&declaration->name),
                           jh_span(" must list only "), callback,
                           jh_span("(...) entries")));
  const size_t first = i + 1;
  int depth = 1;
  size_t index = first;
  char quote = 0;
  while (index < body.length && depth != 0) {
    const char c = body.data[index];
    if (quote != 0) {
      if (c == '\\')
        ++index;
      else if (c == quote)
        quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    }
    ++index;
  }
  if (depth != 0)
    return reject(
        failure, source, variant_code,
        JH_PARTS(jh_span("unbalanced "), callback, jh_span("(...) entry")));
  *entry = sub(body, first, index - 1);
  *position = index;
  return HAL_OK;
}

static bool variant_id(jb_json_span_t text) {
  if (text.length == 0 || text.data[0] < 'A' || text.data[0] > 'Z')
    return false;
  for (size_t i = 1; i < text.length; ++i) {
    const char c = text.data[i];
    if (!((c >= 'A' && c <= 'Z') || jh_ascii_is_digit(c) || c == '_'))
      return false;
  }
  return true;
}

/* "(?:\\.|[^"\\])+" as a whole string. */
static bool string_literal(jb_json_span_t text) {
  if (text.length < 3 || text.data[0] != '"')
    return false;
  size_t i = 1;
  while (i < text.length && text.data[i] != '"')
    i += text.data[i] != '\\' ? 1 : i + 1 < text.length ? 2 : text.length - i;
  return i == text.length - 1;
}

/* NAME, or NAME = VALUE with spaces around "=" allowed. */
static bool variant_definition(jb_json_span_t text, jb_json_span_t *name,
                               jb_json_span_t *value, bool *has_value) {
  if (text.length == 0 || !jh_config_name_start(text.data[0]))
    return false;
  size_t i = 1;
  while (i < text.length && jh_config_name_part(text.data[i]))
    ++i;
  *name = sub(text, 0, i);
  *has_value = false;
  if (i == text.length)
    return true;
  i = jh_config_skip_space(text, i);
  if (i >= text.length || text.data[i] != '=')
    return false;
  *value = sub(text, jh_config_skip_space(text, i + 1), text.length);
  *has_value = true;
  return true;
}

static void variant_destroy(jh_config_variant_t *variant) {
  jb_string_destroy(&variant->id);
  jb_string_destroy(&variant->description);
  jb_string_destroy(&variant->source);
  jh_list_destroy(&variant->definitions);
}

static hal_status_t one_parameter(const jh_config_macro_t *declaration,
                                  const char *name, jh_failure_t *failure) {
  if (declaration->function_like &&
      jb_vector_size(&declaration->parameters) == 1)
    return HAL_OK;
  return reject(failure, jh_string_span(&declaration->source), variant_code,
                JH_PARTS(jh_span(name),
                         jh_span(" must take one parameter, e.g. "),
                         jh_span(name), jh_span("(X)")));
}

/* One X(id, "description", definitions...) entry as a variant. */
static hal_status_t parse_variant(const jh_config_macro_t *declaration,
                                  jb_json_span_t entry,
                                  const jb_vector_t *variants,
                                  jh_config_variant_t *result,
                                  jh_failure_t *failure) {
  const jb_json_span_t source = jh_string_span(&declaration->source);
  jb_vector_t arguments = {0};
  jb_string_t quoted = {0};
  hal_status_t status =
      jb_vector_init(&arguments, sizeof(jb_json_span_t), heap());
  if (status == HAL_OK)
    status = text_init(&quoted);
  if (status == HAL_OK)
    status = split_arguments(entry, &arguments);
  const jb_json_span_t *items = jb_vector_const_data(&arguments);
  const size_t count = jb_vector_size(&arguments);
  if (status == HAL_OK && count < 3) {
    status = jh_python_repr_append(&quoted, entry);
    if (status == HAL_OK)
      status = reject(failure, source, variant_code,
                      JH_PARTS(jh_span("a variant needs an id, a description "
                                       "and at least one definition: "),
                               jh_string_span(&quoted)));
  }
  if (status == HAL_OK) {
    bool seen = false;
    for (size_t i = 0; i < jb_vector_size(variants); ++i)
      seen = seen ||
             jh_span_equal(jh_string_span(&((const jh_config_variant_t *)
                                                jb_vector_const_at(variants, i))
                                               ->id),
                           items[0]);
    if (!variant_id(items[0]) || seen) {
      status = jh_python_repr_append(&quoted, items[0]);
      if (status == HAL_OK)
        status = reject(
            failure, source, variant_code,
            JH_PARTS(jh_span("variant id "), jh_string_span(&quoted),
                     jh_span(" must be unique and use capital letters, digits "
                             "and _, starting with a letter")));
    }
  }
  if (status == HAL_OK && !string_literal(items[1]))
    status = reject(failure, source, variant_code,
                    JH_PARTS(jh_span("variant "), items[0],
                             jh_span(" needs a non-empty string description")));
  if (status == HAL_OK)
    status = text_set(&result->id, items[0]);
  if (status == HAL_OK)
    status =
        text_set(&result->description, sub(items[1], 1, items[1].length - 1));
  if (status == HAL_OK)
    status = text_set(&result->source, source);
  for (size_t i = 2; status == HAL_OK && i < count; ++i) {
    jb_json_span_t name = {"", 0}, value = {"", 0};
    bool has_value = false;
    if (!variant_definition(items[i], &name, &value, &has_value) ||
        is_target_selector(name)) {
      status = jh_python_repr_append(&quoted, items[i]);
      if (status == HAL_OK)
        status = reject(failure, source, variant_code,
                        JH_PARTS(jh_span("variant "), items[0],
                                 jh_span(" has invalid definition "),
                                 jh_string_span(&quoted)));
      break;
    }
    // The -D form: the compiler takes no spaces around "=".
    status = jh_list_push_parts(
        &result->definitions,
        JH_PARTS(name, jh_span(has_value ? "=" : ""), value));
  }
  jb_string_destroy(&quoted);
  jb_vector_destroy(&arguments);
  return status;
}

/* The variants JH_PROJECT_VARIANTS(X) declares; none without declaration. */
static hal_status_t parse_variants(const jh_config_macro_t *declaration,
                                   jb_vector_t *variants,
                                   jh_failure_t *failure) {
  if (declaration == NULL)
    return HAL_OK;
  hal_status_t status = one_parameter(declaration, variants_macro, failure);
  size_t position = 0;
  while (status == HAL_OK) {
    jb_json_span_t entry = {"", 0};
    bool done = false;
    status = x_entry(declaration, &position, &entry, &done, failure);
    if (status != HAL_OK || done)
      break;
    jh_config_variant_t variant = {0};
    status = text_init(&variant.id);
    if (status == HAL_OK)
      status = text_init(&variant.description);
    if (status == HAL_OK)
      status = text_init(&variant.source);
    if (status == HAL_OK)
      status = jh_list_init(&variant.definitions);
    if (status == HAL_OK)
      status = parse_variant(declaration, entry, variants, &variant, failure);
    if (status == HAL_OK)
      status = jb_vector_append(variants, &variant, 1);
    if (status != HAL_OK)
      variant_destroy(&variant);
  }
  return status;
}

/* The selectors JH_PROJECT_TARGETS(X) declares; none without declaration. */
static hal_status_t parse_targets(const jh_config_macro_t *declaration,
                                  jb_vector_t *selectors,
                                  jh_failure_t *failure) {
  if (declaration == NULL)
    return HAL_OK;
  const jb_json_span_t source = jh_string_span(&declaration->source);
  hal_status_t status = one_parameter(declaration, targets_macro, failure);
  size_t position = 0;
  while (status == HAL_OK) {
    jb_json_span_t entry = {"", 0};
    bool done = false;
    status = x_entry(declaration, &position, &entry, &done, failure);
    if (status != HAL_OK || done)
      break;
    const jb_json_span_t selector = strip(entry);
    if (!is_target_selector(selector) ||
        jh_list_contains(selectors, selector)) {
      jb_string_t quoted = {0};
      status = text_init(&quoted);
      if (status == HAL_OK)
        status = jh_python_repr_append(&quoted, selector);
      if (status == HAL_OK)
        status = reject(
            failure, source, variant_code,
            JH_PARTS(jh_span(targets_macro), jh_span(" entry "),
                     jh_string_span(&quoted),
                     jh_span(" must be one HAL_TARGET_* selector, listed "
                             "once")));
      jb_string_destroy(&quoted);
    } else {
      status = jh_list_push(selectors, selector);
    }
  }
  if (status == HAL_OK && jb_vector_size(selectors) == 0)
    status =
        reject(failure, source, variant_code,
               JH_PARTS(jh_span(targets_macro), jh_span(" lists no target")));
  return status;
}

/* _DEFINE of the Python reader: \s*NAME(\([^)]*\))?(.*)$ */
static bool define_parts(jb_json_span_t argument, jb_json_span_t *name,
                         bool *function_like, jb_json_span_t *parameters,
                         jb_json_span_t *rest) {
  size_t i = jh_config_skip_space(argument, 0);
  if (i >= argument.length || !jh_config_name_start(argument.data[i]))
    return false;
  const size_t start = i++;
  while (i < argument.length && jh_config_name_part(argument.data[i]))
    ++i;
  *name = sub(argument, start, i);
  *function_like = false;
  if (i < argument.length && argument.data[i] == '(') {
    const size_t close = find_byte(argument, ')', i + 1);
    if (close < argument.length) {
      *function_like = true;
      *parameters = sub(argument, i + 1, close);
      i = close + 1;
    }
  }
  *rest = sub(argument, i, argument.length);
  return true;
}

/* The macro a #define with argument makes at source. result is initialized
 * and stays with the caller. */
static hal_status_t parse_define(jb_json_span_t argument, jb_json_span_t source,
                                 jh_config_macro_t *result,
                                 jh_failure_t *failure) {
  jb_json_span_t name = {"", 0}, parameters = {"", 0}, rest = {"", 0};
  if (!define_parts(argument, &name, &result->function_like, &parameters,
                    &rest))
    return reject(failure, source, scope_code,
                  JH_PARTS(jh_span("malformed #define")));
  hal_status_t status = text_set(&result->name, name);
  for (size_t begin = 0; status == HAL_OK && result->function_like;) {
    const size_t comma = find_byte(parameters, ',', begin);
    const jb_json_span_t item = strip(sub(parameters, begin, comma));
    if (item.length != 0)
      status = jh_list_push(&result->parameters, item);
    if (comma >= parameters.length)
      break;
    begin = comma + 1;
  }
  if (status == HAL_OK)
    status = text_set(&result->body, strip(rest));
  if (status == HAL_OK)
    status = text_set(&result->source, source);
  return status;
}

static bool quoted_include(jb_json_span_t argument) {
  return argument.length >= 3 && argument.data[0] == '"' &&
         argument.data[argument.length - 1] == '"' &&
         find_byte(argument, '"', 1) == argument.length - 1;
}

/* The resolved file a quoted #include at place names: next to the including
 * file. */
static hal_status_t include_path(jb_json_span_t argument, place_t place,
                                 jb_string_t *out, jh_failure_t *failure) {
  jb_string_t parent = {0}, name = {0}, where = {0};
  hal_status_t status = text_init(&parent);
  if (status == HAL_OK)
    status = text_init(&where);
  if (status == HAL_OK)
    status = text_new(&name, sub(argument, 1, argument.length - 1));
  if (status == HAL_OK)
    status = place_text(&where, place);
  if (status == HAL_OK) {
    status = jh_path_parent(place.path.data, &parent, failure);
    if (status == HAL_OK)
      status = resolve_path(jb_string_data(&name), jb_string_data(&parent), out,
                            failure);
    status = path_failed(failure, status, jh_string_span(&where));
  }
  jb_string_destroy(&where);
  jb_string_destroy(&name);
  jb_string_destroy(&parent);
  return status;
}

/* ── Reading ─────────────────────────────────────────────────────────────── */

typedef struct {
  jb_json_span_t name, argument;
  size_t number;
} directive_line_t;

/* The directives of a file: the lines own the text the directives borrow. */
typedef struct {
  lines_t lines;
  jb_vector_t items;
} directives_t;

static void directives_release(directives_t *directives) {
  jb_vector_destroy(&directives->items);
  lines_destroy(&directives->lines);
}

/* Read the file path and split its directives; release the result after
 * success and after failure. */
static hal_status_t directives_of(const char *path, directives_t *directives,
                                  jh_failure_t *failure) {
  *directives = (directives_t){0};
  jb_file_data_t data = {0};
  hal_status_t status = lines_init(&directives->lines);
  if (status == HAL_OK)
    status =
        jb_vector_init(&directives->items, sizeof(directive_line_t), heap());
  if (status == HAL_OK)
    status = read_text(path, &data, failure);
  if (status == HAL_OK)
    status = logical_lines(file_span(&data), &directives->lines);
  // Only the directive lines stay while the files this one includes are read.
  jb_file_release(&data);
  for (size_t i = 0; status == HAL_OK && i < lines_count(&directives->lines);
       ++i) {
    directive_line_t item = {{"", 0}, {"", 0}, 0};
    const jb_json_span_t text = line_text(&directives->lines, i, &item.number);
    const place_t place = {jh_span(path), item.number};
    bool found = false;
    status = split_directive(text, place, &found, &item.name, &item.argument,
                             failure);
    if (status == HAL_OK && found)
      status = jb_vector_append(&directives->items, &item, 1);
  }
  return status;
}

static bool opens_condition(jb_json_span_t name) {
  return IS(name, "if") || IS(name, "ifdef") || IS(name, "ifndef");
}

/* True when the directives wrap the whole file in a classic include guard. */
static bool include_guard(const jb_vector_t *directives) {
  const directive_line_t *items = jb_vector_const_data(directives);
  const size_t count = jb_vector_size(directives);
  if (count < 3 || !IS(items[0].name, "ifndef"))
    return false;
  jb_json_span_t guard = {"", 0}, parameters = {"", 0}, rest = {"", 0};
  bool function_like = false;
  if (!IS(items[1].name, "define") ||
      !define_parts(items[1].argument, &guard, &function_like, &parameters,
                    &rest) ||
      !jh_span_equal(guard, items[0].argument))
    return false;
  int depth = 0;
  for (size_t index = 0; index < count; ++index) {
    const jb_json_span_t name = items[index].name;
    if (opens_condition(name)) {
      ++depth;
    } else if (depth == 1 && (IS(name, "elif") || IS(name, "elifdef") ||
                              IS(name, "elifndef") || IS(name, "else"))) {
      return false;
    } else if (IS(name, "endif")) {
      if (--depth == 0)
        return index == count - 1;
    }
  }
  return false;
}

/* Syntactic view of the header files, independent of any condition: the
 * names any branch defines or undefines and the two declarations. */
typedef struct {
  names_t defined, seen;
  jh_config_macro_t variants, targets;
  bool has_variants, has_targets;
  jh_failure_t *failure;
} scan_t;

static hal_status_t scan(scan_t *state, const char *path, size_t depth);

static hal_status_t scan_directive(scan_t *state, const char *path,
                                   const directive_line_t *directive,
                                   int *nesting, size_t depth) {
  const jb_json_span_t name = directive->name;
  const place_t place = {jh_span(path), directive->number};
  hal_status_t status = HAL_OK;
  if (opens_condition(name)) {
    ++*nesting;
  } else if (IS(name, "endif")) {
    --*nesting;
  } else if (IS(name, "define") || IS(name, "undef")) {
    jb_json_span_t declared = {"", 0}, parameters = {"", 0}, rest = {"", 0};
    bool function_like = false;
    if (!define_parts(directive->argument, &declared, &function_like,
                      &parameters, &rest))
      return HAL_OK;
    status = names_add(&state->defined, declared, NULL);
    const bool variants = equals(declared, variants_macro);
    if (status != HAL_OK || (!variants && !equals(declared, targets_macro)))
      return status;
    bool *present = variants ? &state->has_variants : &state->has_targets;
    if (!IS(name, "define") || *nesting > 0 || depth != 0 || *present)
      return reject_at(state->failure, place, variant_code,
                       JH_PARTS(jh_span("define "), declared,
                                jh_span(" once, unconditionally, in "),
                                jh_span(header_name)));
    jb_string_t where = {0};
    status = text_init(&where);
    if (status == HAL_OK)
      status = place_text(&where, place);
    if (status == HAL_OK)
      status = parse_define(directive->argument, jh_string_span(&where),
                            variants ? &state->variants : &state->targets,
                            state->failure);
    if (status == HAL_OK)
      *present = true;
    jb_string_destroy(&where);
  } else if (IS(name, "include") && quoted_include(directive->argument)) {
    jb_string_t included = {0};
    status = text_init(&included);
    if (status == HAL_OK)
      status =
          include_path(directive->argument, place, &included, state->failure);
    if (status == HAL_OK)
      status = scan(state, jb_string_data(&included), depth + 1);
    jb_string_destroy(&included);
  }
  return status;
}

/* Scan the resolved file path and the files it includes. */
static hal_status_t scan(scan_t *state, const char *path, size_t depth) {
  if (names_contain(&state->seen, jh_span(path)))
    return HAL_OK;
  // The scan follows every #include, also of a branch that is not taken.
  // Memory fails here as everywhere; any other file the system does not
  // show is left to the reader, which reports it where it is read.
  bool regular = false;
  const hal_status_t inspected = regular_file(path, &regular);
  if (inspected == HAL_ENOMEM)
    return cannot_read(state->failure, inspected, path);
  if (!regular)
    return HAL_OK;
  if (depth > MAX_MACRO_NESTING)
    return limit(state->failure, jh_span(path),
                 JH_PARTS(jh_span("includes nest more than 1000 levels")));
  directives_t directives = {0};
  hal_status_t status = names_add(&state->seen, jh_span(path), NULL);
  if (status != HAL_OK)
    return status;
  status = directives_of(path, &directives, state->failure);
  // Declarations inside the file's own include guard are unconditional.
  int nesting = status == HAL_OK && include_guard(&directives.items) ? -1 : 0;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&directives.items);
       ++i)
    status = scan_directive(
        state, path, jb_vector_const_at(&directives.items, i), &nesting, depth);
  directives_release(&directives);
  return status;
}

static hal_status_t scan_init(scan_t *state, jh_failure_t *failure) {
  *state = (scan_t){0};
  state->failure = failure;
  hal_status_t status = names_init(&state->defined);
  if (status == HAL_OK)
    status = names_init(&state->seen);
  if (status == HAL_OK)
    status = macro_init(&state->variants);
  if (status == HAL_OK)
    status = macro_init(&state->targets);
  return status;
}

static void scan_destroy(scan_t *state) {
  names_destroy(&state->defined);
  names_destroy(&state->seen);
  macro_destroy(&state->variants);
  macro_destroy(&state->targets);
}

/* One open #if: unknown holds the names its conditions depend on. */
typedef struct {
  bool parent_active, active, taken;
  names_t unknown;
} frame_t;

/* What decides a name while the files are read. */
typedef struct {
  jb_string_t name;
  names_t unknown;
} tracked_t;

/* frames and tracked are maps because their entries hold sets, which must
 * stay where they are while they are in use. */
typedef struct {
  jh_project_config_t *config;
  names_t known;    /* names every build passes or the files define */
  jb_map_t frames;  /* the open conditions: frame_t by its depth */
  jb_map_t tracked; /* tracked_t by its name, which the key borrows */
  names_t once;     /* files with #pragma once */
  size_t work_left;
  jh_failure_t *failure;
} reader_t;

static hal_status_t reader_init(reader_t *reader, jh_project_config_t *config,
                                jh_failure_t *failure) {
  *reader = (reader_t){0};
  reader->config = config;
  reader->failure = failure;
  reader->work_left = config->max_work;
  hal_status_t status = names_init(&reader->known);
  if (status == HAL_OK)
    status = jb_map_init(&reader->frames, sizeof(size_t), sizeof(frame_t),
                         index_order, NULL, heap());
  if (status == HAL_OK)
    status = name_map_init(&reader->tracked, sizeof(tracked_t));
  if (status == HAL_OK)
    status = names_init(&reader->once);
  return status;
}

static size_t reader_depth(const reader_t *reader) {
  return jb_map_size(&reader->frames);
}

/* The innermost open condition; the caller checked that there is one. */
static frame_t *reader_top(const reader_t *reader) {
  const size_t last = reader_depth(reader) - 1;
  void *frame = NULL;
  (void)jb_map_find(&reader->frames, &last, &frame);
  return frame;
}

static bool reader_active(const reader_t *reader) {
  return reader_depth(reader) == 0 || reader_top(reader)->active;
}

/* Open a condition; *frame is its place, with an empty set of names. */
static hal_status_t reader_push(reader_t *reader, bool parent_active,
                                bool active, frame_t **frame) {
  const size_t depth = reader_depth(reader);
  const frame_t opened = {parent_active, active, active, {{0}, {0}}};
  void *slot = NULL;
  hal_status_t status = jb_map_insert(&reader->frames, &depth, &opened);
  if (status == HAL_OK)
    status = jb_map_find(&reader->frames, &depth, &slot);
  if (status != HAL_OK)
    return status;
  *frame = slot;
  // The set starts in its place.
  return names_init(&(*frame)->unknown);
}

/* Close the innermost condition. */
static void reader_pop(reader_t *reader) {
  const size_t last = reader_depth(reader) - 1;
  names_destroy(&reader_top(reader)->unknown);
  (void)jb_map_erase(&reader->frames, &last);
}

/* What decides name so far, or NULL. */
static tracked_t *reader_tracked(const reader_t *reader, jb_json_span_t name) {
  void *found = NULL;
  return jb_map_find(&reader->tracked, &name, &found) == HAL_OK ? found : NULL;
}

/* Start tracking name with an empty set; *tracked is its place. */
static hal_status_t tracked_add(reader_t *reader, jb_json_span_t name,
                                tracked_t **tracked) {
  tracked_t item = {{0}, {{0}, {0}}};
  hal_status_t status = text_new(&item.name, name);
  const jb_json_span_t key = jh_string_span(&item.name);
  if (status == HAL_OK)
    status = jb_map_insert(&reader->tracked, &key, &item);
  if (status != HAL_OK) {
    jb_string_destroy(&item.name);
    return status;
  }
  *tracked = reader_tracked(reader, key);
  // The set starts in its place.
  return names_init(&(*tracked)->unknown);
}

static void tracked_drop(reader_t *reader, tracked_t *tracked) {
  // The key borrows the name, which goes last.
  jb_string_t name = tracked->name;
  const jb_json_span_t key = jh_string_span(&name);
  names_destroy(&tracked->unknown);
  (void)jb_map_erase(&reader->tracked, &key);
  jb_string_destroy(&name);
}

static void reader_destroy(reader_t *reader) {
  while (reader_depth(reader) != 0)
    reader_pop(reader);
  jb_map_destroy(&reader->frames);
  jb_map_iter_t first = {0};
  while (jb_map_first(&reader->tracked, &first) == HAL_OK) {
    const void *key = NULL, *value = NULL;
    if (jb_map_iter_get(&first, &key, &value) != HAL_OK)
      break;
    tracked_drop(reader, reader_tracked(reader, *(const jb_json_span_t *)key));
  }
  jb_map_destroy(&reader->tracked);
  names_destroy(&reader->known);
  names_destroy(&reader->once);
}

/* The dependency entry of name in the result, or NULL. */
static const jh_config_dependency_t *
dependency_find(const jb_vector_t *dependencies, jb_json_span_t name) {
  const jh_config_dependency_t *items = jb_vector_const_data(dependencies);
  size_t low = 0, high = jb_vector_size(dependencies);
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    const int order =
        jh_span_compare(jh_string_span(&items[middle].name), name);
    if (order == 0)
      return &items[middle];
    if (order < 0)
      low = middle + 1;
    else
      high = middle;
  }
  return NULL;
}

const jb_vector_t *
jh_project_config_uncertain(const jh_project_config_t *config,
                            jb_json_span_t name) {
  const jh_config_dependency_t *found =
      config != NULL ? dependency_find(&config->dependencies, name) : NULL;
  return found != NULL ? &found->unknown : NULL;
}

static void dependency_destroy(jh_config_dependency_t *dependency) {
  jb_string_destroy(&dependency->name);
  jh_list_destroy(&dependency->unknown);
}

/* Give the result what decides each name, in the order of the names. */
static hal_status_t reader_publish(const reader_t *reader) {
  jb_vector_t *dependencies = &reader->config->dependencies;
  hal_status_t status = HAL_OK;
  jb_map_iter_t at = {0};
  for (hal_status_t more = jb_map_first(&reader->tracked, &at);
       status == HAL_OK && more == HAL_OK; more = jb_map_next(&at)) {
    const void *key = NULL, *value = NULL;
    status = jb_map_iter_get(&at, &key, &value);
    if (status != HAL_OK)
      break;
    const tracked_t *tracked = value;
    jh_config_dependency_t item = {{0}, {0}};
    status = text_new(&item.name, jh_string_span(&tracked->name));
    if (status == HAL_OK)
      status = jh_list_init(&item.unknown);
    if (status == HAL_OK)
      status = names_to_list(&tracked->unknown, &item.unknown);
    if (status == HAL_OK)
      status = jb_vector_append(dependencies, &item, 1);
    if (status != HAL_OK)
      dependency_destroy(&item);
  }
  return status;
}

/* Names the reader cannot see that decide text here, through the macros it
 * expands to and the conditions that defined them. */
static hal_status_t unknown_in(reader_t *reader, jb_json_span_t text,
                               jb_json_span_t where, names_t *unknown) {
  names_t names = {0};
  work_budget_t budget = {&reader->work_left, reader->config->max_work};
  const jb_json_span_t none = {"", 0};
  hal_status_t status = names_init(&names);
  if (status == HAL_OK)
    status = referenced_names(reader->config, text, none, &names, where,
                              &budget, reader->failure);
  names_walk_t walk = {0};
  jb_json_span_t name = {"", 0};
  while (status == HAL_OK && names_next(&names, &walk, &name)) {
    if (!names_contain(&reader->known, name) && !jh_feature_symbol(name, false))
      status = names_add(unknown, name, NULL);
    // What decides a name decides the text that uses it.
    const tracked_t *tracked = reader_tracked(reader, name);
    if (status == HAL_OK && tracked != NULL)
      status = names_merge(unknown, &tracked->unknown);
  }
  names_destroy(&names);
  return status;
}

/* Whether an open condition depends on a name the reader cannot see; with
 * into, those names are added to it. */
static hal_status_t frames_unknown(const reader_t *reader, names_t *into,
                                   bool *any) {
  hal_status_t status = HAL_OK;
  jb_map_iter_t at = {0};
  *any = false;
  for (hal_status_t more = jb_map_first(&reader->frames, &at);
       status == HAL_OK && more == HAL_OK; more = jb_map_next(&at)) {
    const void *key = NULL, *value = NULL;
    status = jb_map_iter_get(&at, &key, &value);
    if (status != HAL_OK)
      break;
    const names_t *unknown = &((const frame_t *)value)->unknown;
    *any = *any || names_size(unknown) != 0;
    if (into != NULL)
      status = names_merge(into, unknown);
  }
  return status;
}

/* Record what decides name here; an exact active definition replaces an
 * earlier uncertain one. */
static hal_status_t reader_note(reader_t *reader, jb_json_span_t name) {
  bool uncertain = false;
  hal_status_t status = frames_unknown(reader, NULL, &uncertain);
  tracked_t *tracked = reader_tracked(reader, name);
  if (status == HAL_OK && uncertain) {
    if (tracked == NULL)
      status = tracked_add(reader, name, &tracked);
    if (status == HAL_OK)
      status = frames_unknown(reader, &tracked->unknown, &uncertain);
  } else if (status == HAL_OK && tracked != NULL && reader_active(reader)) {
    tracked_drop(reader, tracked);
  }
  return status;
}

static bool tests_definition(jb_json_span_t kind) {
  return IS(kind, "ifdef") || IS(kind, "ifndef") || IS(kind, "elifdef") ||
         IS(kind, "elifndef");
}

/* The names a condition depends on: of #ifdef NAME as of "defined NAME". */
static hal_status_t condition_unknown(reader_t *reader, jb_json_span_t kind,
                                      jb_json_span_t argument,
                                      jb_json_span_t where, names_t *unknown) {
  if (!tests_definition(kind))
    return unknown_in(reader, argument, where, unknown);
  jb_string_t tested = {0};
  hal_status_t status = text_init(&tested);
  if (status == HAL_OK)
    status = jh_join(&tested, JH_PARTS(jh_span("defined "), argument));
  if (status == HAL_OK)
    status = unknown_in(reader, jh_string_span(&tested), where, unknown);
  jb_string_destroy(&tested);
  return status;
}

static hal_status_t reader_condition(reader_t *reader, jb_json_span_t kind,
                                     jb_json_span_t argument,
                                     jb_json_span_t where, bool *result) {
  if (tests_definition(kind)) {
    if (!jh_config_identifier(argument))
      return reject(reader->failure, where, scope_code,
                    JH_PARTS(jh_span("#"), kind, jh_span(" needs one name")));
    *result = (jh_project_config_find(reader->config, argument) != NULL) ==
              (IS(kind, "ifdef") || IS(kind, "elifdef"));
    return HAL_OK;
  }
  work_budget_t budget = {&reader->work_left, reader->config->max_work};
  int64_t value = 0;
  bool range = false;
  hal_status_t status = evaluate(reader->config, argument, where, &budget,
                                 &value, &range, reader->failure);
  if (status != HAL_OK && range)
    status = limit(reader->failure, where,
                   JH_PARTS(jh_string_span(&reader->failure->message),
                            jh_span(" exceeds the signed 64-bit range of jh")));
  *result = value != 0;
  return status;
}

static hal_status_t reader_read(reader_t *reader, const char *path,
                                size_t depth);

/* The frame an #elif, #else or #endif belongs to. */
static hal_status_t reader_frame(reader_t *reader, jb_json_span_t name,
                                 jb_json_span_t where, frame_t **frame) {
  if (reader_depth(reader) == 0)
    return reject(reader->failure, where, scope_code,
                  JH_PARTS(jh_span("#"), name, jh_span(" without #if")));
  *frame = reader_top(reader);
  return HAL_OK;
}

static hal_status_t reader_open(reader_t *reader, jb_json_span_t name,
                                jb_json_span_t argument, jb_json_span_t where) {
  const bool parent_active = reader_active(reader);
  bool active = false;
  frame_t *frame = NULL;
  hal_status_t status = HAL_OK;
  if (parent_active)
    status = reader_condition(reader, name, argument, where, &active);
  // A failure ends the read: the reader closes what is open.
  if (status == HAL_OK)
    status = reader_push(reader, parent_active, active, &frame);
  if (status == HAL_OK)
    status = condition_unknown(reader, name, argument, where, &frame->unknown);
  return status;
}

static hal_status_t reader_define(reader_t *reader, jb_json_span_t argument,
                                  jb_json_span_t where) {
  jh_config_macro_t definition = {0};
  hal_status_t status = macro_init(&definition);
  if (status == HAL_OK)
    status = parse_define(argument, where, &definition, reader->failure);
  if (status == HAL_OK)
    status = reader_note(reader, jh_string_span(&definition.name));
  if (status == HAL_OK && reader_active(reader))
    status = macro_set(reader->config, &definition);
  macro_destroy(&definition);
  return status;
}

static hal_status_t reader_directive(reader_t *reader, jb_json_span_t name,
                                     jb_json_span_t argument, place_t place,
                                     size_t depth) {
  jh_project_config_t *config = reader->config;
  jb_string_t text = {0};
  hal_status_t status = text_init(&text);
  if (status == HAL_OK)
    status = place_text(&text, place);
  const jb_json_span_t where = jh_string_span(&text);
  frame_t *frame = NULL;
  if (status != HAL_OK) {
    // Nothing to do without the place.
  } else if (opens_condition(name)) {
    status = reader_open(reader, name, argument, where);
  } else if (IS(name, "elif") || IS(name, "elifdef") || IS(name, "elifndef")) {
    status = reader_frame(reader, name, where, &frame);
    if (status == HAL_OK)
      status =
          condition_unknown(reader, name, argument, where, &frame->unknown);
    bool result = false;
    if (status == HAL_OK && frame->parent_active && !frame->taken)
      status = reader_condition(reader, name, argument, where, &result);
    if (status == HAL_OK) {
      frame->active = result;
      frame->taken = frame->taken || result;
    }
  } else if (IS(name, "else")) {
    status = reader_frame(reader, name, where, &frame);
    if (status == HAL_OK) {
      frame->active = frame->parent_active && !frame->taken;
      frame->taken = true;
    }
  } else if (IS(name, "endif")) {
    status = reader_frame(reader, name, where, &frame);
    if (status == HAL_OK)
      reader_pop(reader);
  } else if (IS(name, "define")) {
    status = reader_define(reader, argument, where);
  } else if (IS(name, "undef")) {
    if (!jh_config_identifier(argument))
      status = reject(reader->failure, where, scope_code,
                      JH_PARTS(jh_span("#undef needs one name")));
    if (status == HAL_OK)
      status = reader_note(reader, argument);
    if (status == HAL_OK && reader_active(reader))
      macro_erase(config, argument);
  } else if (!reader_active(reader)) {
    // Other directives of a skipped branch do nothing.
  } else if (IS(name, "include")) {
    jb_string_t included = {0};
    status = text_init(&included);
    if (status == HAL_OK && !quoted_include(argument))
      status = reject(reader->failure, where, scope_code,
                      JH_PARTS(jh_span("only #include \"file\" relative to "
                                       "the project configuration is "
                                       "supported")));
    if (status == HAL_OK)
      status = include_path(argument, place, &included, reader->failure);
    if (status == HAL_OK)
      status = reader_read(reader, jb_string_data(&included), depth + 1);
    jb_string_destroy(&included);
  } else if (IS(name, "error")) {
    config->stopped = true;
    status = text_set(&config->error_message, argument);
    if (status == HAL_OK)
      status = text_set(&config->error_source, where);
  } else if (IS(name, "pragma")) {
    if (IS(argument, "once"))
      status = names_add(&reader->once, place.path, NULL);
  } else if (!IS(name, "warning") && !IS(name, "line")) {
    status = reject(reader->failure, where, scope_code,
                    JH_PARTS(jh_span("unsupported #"), name));
  }
  jb_string_destroy(&text);
  return status;
}

/* Read the resolved file path. */
static hal_status_t reader_read(reader_t *reader, const char *path,
                                size_t depth) {
  jh_project_config_t *config = reader->config;
  const jb_json_span_t name = jh_span(path);
  if (names_contain(&reader->once, name) || config->stopped)
    return HAL_OK;
  if (depth > MAX_INCLUDE_DEPTH)
    return reject(reader->failure, name, scope_code,
                  JH_PARTS(jh_span("includes nest too deeply")));
  hal_status_t status = HAL_OK;
  if (!jh_list_contains(&config->files, name))
    status = jh_list_push(&config->files, name);
  jb_file_data_t data = {0};
  lines_t lines = {0};
  if (status == HAL_OK)
    status = lines_init(&lines);
  if (status == HAL_OK)
    status = read_text(path, &data, reader->failure);
  if (status == HAL_OK)
    status = logical_lines(file_span(&data), &lines);
  // Only the directive lines stay while the files this one includes are read.
  jb_file_release(&data);
  const size_t base_depth = reader_depth(reader);
  for (size_t i = 0;
       status == HAL_OK && !config->stopped && i < lines_count(&lines); ++i) {
    place_t place = {name, 0};
    const jb_json_span_t text = line_text(&lines, i, &place.number);
    jb_json_span_t directive = {"", 0}, argument = {"", 0};
    bool found = false;
    status = split_directive(text, place, &found, &directive, &argument,
                             reader->failure);
    if (status == HAL_OK && found)
      status = reader_directive(reader, directive, argument, place, depth);
  }
  if (status == HAL_OK && !config->stopped &&
      reader_depth(reader) != base_depth)
    status = reject(reader->failure, name, scope_code,
                    JH_PARTS(jh_span("unterminated #if")));
  lines_destroy(&lines);
  return status;
}

/* The macros the -D definitions make. */
static hal_status_t predefined_macros(jh_project_config_t *config,
                                      const jb_vector_t *definitions,
                                      jh_failure_t *failure) {
  hal_status_t status = HAL_OK;
  for (size_t index = 0; status == HAL_OK && definitions != NULL &&
                         index < jb_vector_size(definitions);
       ++index) {
    const jb_json_span_t definition =
        jh_string_span(jh_list_at(definitions, index));
    const jb_json_span_t text = strip(definition);
    size_t i = 0;
    while (i < text.length && jh_config_name_part(text.data[i]))
      ++i;
    if (text.length == 0 || !jh_config_name_start(text.data[0]) ||
        (i < text.length && text.data[i] != '=')) {
      jb_string_t quoted = {0};
      status = text_init(&quoted);
      if (status == HAL_OK)
        status = jh_python_repr_append(&quoted, definition);
      if (status == HAL_OK)
        status = jh_fail_coded(
            failure, HAL_ECONFIG, 2, "JH-CFG-VALUE", jh_span("predefined"),
            JH_PARTS(jh_span("invalid definition "), jh_string_span(&quoted)));
      jb_string_destroy(&quoted);
      break;
    }
    jh_config_macro_t value = {0};
    status = macro_init(&value);
    if (status == HAL_OK)
      status = text_set(&value.name, sub(text, 0, i));
    if (status == HAL_OK)
      status = text_set(&value.body, i < text.length
                                         ? strip(sub(text, i + 1, text.length))
                                         : jh_span("1"));
    if (status == HAL_OK)
      status = text_set(&value.source, jh_span("predefined"));
    if (status == HAL_OK)
      status = macro_set(config, &value);
    macro_destroy(&value);
  }
  return status;
}

static void directive_destroy(jh_config_directive_t *directive) {
  jb_string_destroy(&directive->file);
  jb_string_destroy(&directive->name);
  jb_string_destroy(&directive->text);
}

/* Append #define/#undef of the resolved file path, for the checks that look
 * at any branch. */
static hal_status_t file_directives(const char *path, jb_vector_t *result,
                                    jh_failure_t *failure) {
  directives_t directives = {0};
  hal_status_t status = directives_of(path, &directives, failure);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&directives.items);
       ++i) {
    const directive_line_t *item = jb_vector_const_at(&directives.items, i);
    const bool undef = IS(item->name, "undef");
    jb_json_span_t name = {"", 0}, parameters = {"", 0}, rest = {"", 0};
    bool function_like = false;
    if ((!undef && !IS(item->name, "define")) ||
        !define_parts(item->argument, &name, &function_like, &parameters,
                      &rest))
      continue;
    const size_t after = jh_config_skip_space(item->argument, 0) + name.length;
    jh_config_directive_t directive = {{0}, {0}, {0}, item->number, undef};
    status = text_new(&directive.file, jh_span(path));
    if (status == HAL_OK)
      status = text_new(&directive.name, name);
    if (status == HAL_OK)
      status =
          text_new(&directive.text,
                   strip(sub(item->argument, after, item->argument.length)));
    if (status == HAL_OK)
      status = jb_vector_append(result, &directive, 1);
    if (status != HAL_OK)
      directive_destroy(&directive);
  }
  directives_release(&directives);
  return status;
}

/* The unresolved names of definition (an object-like macro with a body),
 * charged to the budget of one query: appended to result in code point
 * order. */
static hal_status_t unresolved_names(const jh_project_config_t *config,
                                     const jh_config_macro_t *definition,
                                     work_budget_t *budget, jb_vector_t *result,
                                     jh_failure_t *failure) {
  names_t names = {0}, unresolved = {0};
  hal_status_t status = names_init(&names);
  if (status == HAL_OK)
    status = names_init(&unresolved);
  if (status == HAL_OK)
    status =
        referenced_names(config, jh_string_span(&definition->body),
                         jh_string_span(&definition->name), &names,
                         jh_string_span(&definition->source), budget, failure);
  names_walk_t walk = {0};
  jb_json_span_t name = {"", 0};
  while (status == HAL_OK && names_next(&names, &walk, &name)) {
    const jh_config_macro_t *used = jh_project_config_find(config, name);
    if (used == NULL || used->function_like)
      status = names_add(&unresolved, name, NULL);
    // What decides a name decides the value that uses it.
    const jb_vector_t *decided = jh_project_config_uncertain(config, name);
    if (status == HAL_OK && decided != NULL)
      status = names_add_list(&unresolved, decided);
  }
  if (status == HAL_OK)
    status = names_to_list(&unresolved, result);
  names_destroy(&unresolved);
  names_destroy(&names);
  return status;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

jh_config_limits_t jh_config_default_limits(void) {
  const jh_config_limits_t limits = {1000000};
  return limits;
}

void jh_project_config_init(jh_project_config_t *config) {
  *config = (jh_project_config_t){0};
  // Initialization only checks its arguments, which are valid here.
  (void)jb_vector_init(&config->macros, sizeof(jh_config_macro_t), heap());
  (void)name_map_init(&config->macro_names, sizeof(size_t));
  (void)jb_vector_init(&config->variants, sizeof(jh_config_variant_t), heap());
  (void)jh_list_init(&config->files);
  (void)jh_list_init(&config->targets);
  (void)jh_config_directives_init(&config->directives);
  (void)jb_vector_init(&config->dependencies, sizeof(jh_config_dependency_t),
                       heap());
  (void)text_init(&config->error_message);
  (void)text_init(&config->error_source);
}

void jh_project_config_destroy(jh_project_config_t *config) {
  for (size_t i = 0; i < jb_vector_size(&config->macros); ++i)
    macro_destroy(jb_vector_at(&config->macros, i));
  for (size_t i = 0; i < jb_vector_size(&config->variants); ++i)
    variant_destroy(jb_vector_at(&config->variants, i));
  for (size_t i = 0; i < jb_vector_size(&config->dependencies); ++i)
    dependency_destroy(jb_vector_at(&config->dependencies, i));
  jb_vector_destroy(&config->macros);
  jb_map_destroy(&config->macro_names);
  jb_vector_destroy(&config->variants);
  jh_list_destroy(&config->files);
  jh_list_destroy(&config->targets);
  jh_config_directives_destroy(&config->directives);
  jb_vector_destroy(&config->dependencies);
  jb_string_destroy(&config->error_message);
  jb_string_destroy(&config->error_source);
  *config = (jh_project_config_t){0};
}

hal_status_t jh_config_directives_init(jb_vector_t *directives) {
  return jb_vector_init(directives, sizeof(jh_config_directive_t), heap());
}

void jh_config_directives_destroy(jb_vector_t *directives) {
  for (size_t i = 0; i < jb_vector_size(directives); ++i)
    directive_destroy(jb_vector_at(directives, i));
  jb_vector_destroy(directives);
}

void jh_config_value_init(jh_config_value_t *value) {
  *value = (jh_config_value_t){JH_CONFIG_INTEGER_NONE, 0, {0}};
  (void)jh_list_init(&value->unresolved);
}

void jh_config_value_destroy(jh_config_value_t *value) {
  jh_list_destroy(&value->unresolved);
  *value = (jh_config_value_t){JH_CONFIG_INTEGER_NONE, 0, {0}};
}

hal_status_t jh_project_config_value(const jh_project_config_t *config,
                                     jb_json_span_t name,
                                     jh_config_value_t *value,
                                     jh_failure_t *failure) {
  if (config == NULL || value == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  failure_reset(failure);
  const jh_config_macro_t *definition = jh_project_config_find(config, name);
  if (definition == NULL || definition->function_like ||
      jb_string_size(&definition->body) == 0)
    return HAL_OK;
  // One budget for the whole query, none shared with other queries.
  size_t left = config->max_work;
  work_budget_t budget = {&left, config->max_work};
  hal_status_t status = unresolved_names(config, definition, &budget,
                                         &value->unresolved, failure);
  if (status != HAL_OK) {
    // A failed query leaves no part of an answer.
    jh_config_value_destroy(value);
    jh_config_value_init(value);
    return finish(failure, status);
  }
  if (jb_vector_size(&value->unresolved) != 0)
    return HAL_OK;
  int64_t integer = 0;
  bool range = false;
  status = evaluate(config, jh_string_span(&definition->body),
                    jh_string_span(&definition->source), &budget, &integer,
                    &range, failure);
  if (status == HAL_OK) {
    value->kind = JH_CONFIG_INTEGER_VALUE;
    value->integer = integer;
  } else if (range) {
    value->kind = JH_CONFIG_INTEGER_OUT_OF_RANGE;
  } else if (status != HAL_ECONFIG) {
    // Invalid literals and the limits of jh still fail.
    return finish(failure, status);
  }
  // A body the reader cannot evaluate has no value.
  failure_reset(failure);
  return HAL_OK;
}

hal_status_t jh_project_config_directives(const char *path,
                                          jb_vector_t *directives,
                                          jh_failure_t *failure) {
  if (path == NULL || directives == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  failure_reset(failure);
  bool regular = false;
  hal_status_t status = named_file(path, &regular, failure);
  if (status != HAL_OK || !regular)
    return status;
  jb_string_t resolved = {0};
  status = text_init(&resolved);
  if (status == HAL_OK)
    status = path_failed(failure, resolve_path(path, NULL, &resolved, failure),
                         jh_span(path));
  if (status == HAL_OK)
    status = file_directives(jb_string_data(&resolved), directives, failure);
  jb_string_destroy(&resolved);
  return finish(failure, status);
}

/* Read the file input names, resolved first. */
static hal_status_t read_input(reader_t *reader, const char *input) {
  jb_string_t path = {0};
  hal_status_t status = text_init(&path);
  if (status == HAL_OK)
    status = path_failed(reader->failure,
                         resolve_path(input, NULL, &path, reader->failure),
                         jh_span(input));
  if (status == HAL_OK)
    status = reader_read(reader, jb_string_data(&path), 0);
  jb_string_destroy(&path);
  return status;
}

static hal_status_t scan_input(scan_t *state, const char *input) {
  jb_string_t path = {0};
  hal_status_t status = text_init(&path);
  if (status == HAL_OK)
    status = path_failed(state->failure,
                         resolve_path(input, NULL, &path, state->failure),
                         jh_span(input));
  if (status == HAL_OK)
    status = scan(state, jb_string_data(&path), 0);
  jb_string_destroy(&path);
  return status;
}

/* The names every condition may use: of the files, of the -D definitions
 * (the macros before the files are read), the target selectors and what the
 * variants define. */
static hal_status_t known_names(reader_t *reader, const scan_t *header,
                                const names_t *early, bool project) {
  const jh_project_config_t *config = reader->config;
  hal_status_t status = names_merge(&reader->known, early);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&config->macros);
       ++i)
    status = names_add(&reader->known,
                       jh_string_span(&macro_at(config, i)->name), NULL);
  if (status != HAL_OK || !project)
    return status;
  status = names_merge(&reader->known, &header->defined);
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(target_selectors); ++i)
    status = names_add(&reader->known, jh_span(target_selectors[i]), NULL);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&config->variants);
       ++i) {
    const jh_config_variant_t *variant =
        jb_vector_const_at(&config->variants, i);
    for (size_t j = 0;
         status == HAL_OK && j < jb_vector_size(&variant->definitions); ++j) {
      const jb_json_span_t definition =
          jh_string_span(jh_list_at(&variant->definitions, j));
      status =
          names_add(&reader->known,
                    sub(definition, 0, find_byte(definition, '=', 0)), NULL);
    }
  }
  return status;
}

hal_status_t
jh_project_config_read(const char *header, const jb_vector_t *definitions,
                       const char *preinclude, const jh_config_limits_t *limits,
                       jh_project_config_t *config, jh_failure_t *failure) {
  if (header == NULL || preinclude == NULL || config == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  failure_reset(failure);
  config->max_work =
      limits != NULL ? limits->max_work : jh_config_default_limits().max_work;
  hal_status_t status = predefined_macros(config, definitions, failure);
  bool project = false;
  if (status == HAL_OK)
    status = named_file(header, &project, failure);
  if (status != HAL_OK || (!project && preinclude[0] == '\0'))
    return finish(failure, status);
  scan_t early = {0}, declared = {0};
  reader_t reader = {0};
  status = scan_init(&early, failure);
  if (status == HAL_OK)
    status = scan_init(&declared, failure);
  if (status == HAL_OK)
    status = reader_init(&reader, config, failure);
  if (status == HAL_OK && preinclude[0] != '\0')
    status = scan_input(&early, preinclude);
  if (status == HAL_OK && project) {
    status = scan_input(&declared, header);
    if (status == HAL_OK)
      status = parse_variants(declared.has_variants ? &declared.variants : NULL,
                              &config->variants, failure);
    if (status == HAL_OK)
      status = parse_targets(declared.has_targets ? &declared.targets : NULL,
                             &config->targets, failure);
  }
  if (status == HAL_OK)
    status = known_names(&reader, &declared, &early.defined, project);
  if (status == HAL_OK && preinclude[0] != '\0')
    status = read_input(&reader, preinclude);
  if (status == HAL_OK && project)
    status = read_input(&reader, header);
  macros_close_gaps(config);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&config->files);
       ++i)
    status = file_directives(jb_string_data(jh_list_at(&config->files, i)),
                             &config->directives, failure);
  // The result of a preinclude alone carries no dependencies.
  if (status == HAL_OK && project)
    status = reader_publish(&reader);
  reader_destroy(&reader);
  scan_destroy(&declared);
  scan_destroy(&early);
  return finish(failure, status);
}

/* ── The result of config inspect ────────────────────────────────────────── */

static hal_status_t write_key(jb_json_writer_t *writer, const char *key) {
  return jb_json_writer_key(writer, jh_span(key));
}

static hal_status_t write_text(jb_json_writer_t *writer, const char *key,
                               jb_json_span_t value) {
  const hal_status_t status = write_key(writer, key);
  return status == HAL_OK ? jb_json_writer_string(writer, value) : status;
}

static hal_status_t write_string(jb_json_writer_t *writer, const char *key,
                                 const jb_string_t *value) {
  return write_text(writer, key, jh_string_span(value));
}

static hal_status_t write_flag(jb_json_writer_t *writer, const char *key,
                               bool value) {
  const hal_status_t status = write_key(writer, key);
  return status == HAL_OK ? jb_json_writer_bool(writer, value) : status;
}

static hal_status_t write_count(jb_json_writer_t *writer, const char *key,
                                size_t value) {
  const hal_status_t status = write_key(writer, key);
  return status == HAL_OK ? jb_json_writer_u64(writer, value) : status;
}

static hal_status_t write_list(jb_json_writer_t *writer, const char *key,
                               const jb_vector_t *list) {
  hal_status_t status = write_key(writer, key);
  if (status == HAL_OK)
    status = jb_json_writer_array_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(list); ++i)
    status = jb_json_writer_string(writer, jh_string_span(jh_list_at(list, i)));
  return status == HAL_OK ? jb_json_writer_array_end(writer) : status;
}

static hal_status_t open_array(jb_json_writer_t *writer, const char *key) {
  const hal_status_t status = write_key(writer, key);
  return status == HAL_OK ? jb_json_writer_array_begin(writer) : status;
}

hal_status_t jh_config_write_directives(jb_json_writer_t *writer,
                                        const jb_vector_t *directives) {
  hal_status_t status = open_array(writer, "directives");
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(directives); ++i) {
    const jh_config_directive_t *item = jb_vector_const_at(directives, i);
    status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status = write_string(writer, "file", &item->file);
    if (status == HAL_OK)
      status = write_count(writer, "line", item->line);
    if (status == HAL_OK)
      status = write_text(writer, "directive",
                          jh_span(item->undef ? "undef" : "define"));
    if (status == HAL_OK)
      status = write_string(writer, "name", &item->name);
    if (status == HAL_OK)
      status = write_string(writer, "text", &item->text);
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  }
  return status == HAL_OK ? jb_json_writer_array_end(writer) : status;
}

/* One macro with the value the build reads. Each value is a query with a
 * budget of its own. A value the reader cannot give (an invalid literal, a
 * jh limit) fails only that macro, and the caller decides: configure stops
 * on an invalid literal, and on a value over a limit only when the build
 * reads it. */
static hal_status_t write_macro(jb_json_writer_t *writer,
                                const jh_project_config_t *config,
                                const jh_config_macro_t *macro,
                                jh_failure_t *failure) {
  jh_config_value_t value = {0};
  jh_failure_t error = {0};
  jh_config_value_init(&value);
  jh_failure_init(&error);
  const hal_status_t read = jh_project_config_value(
      config, jh_string_span(&macro->name), &value, &error);
  hal_status_t status = HAL_OK;
  if (read != HAL_OK && jb_string_size(&error.message) == 0)
    status = jh_fail_storage(failure, read);
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(writer);
  if (status == HAL_OK)
    status = write_string(writer, "name", &macro->name);
  if (status == HAL_OK)
    status = write_string(writer, "body", &macro->body);
  if (status == HAL_OK)
    status = write_string(writer, "source", &macro->source);
  if (status == HAL_OK)
    status = write_flag(writer, "functionLike", macro->function_like);
  if (status == HAL_OK)
    status = write_list(writer, "parameters", &macro->parameters);
  if (status == HAL_OK && read != HAL_OK) {
    status = write_text(writer, "integerState", jh_span("error"));
    if (status == HAL_OK)
      status = write_key(writer, "integerError");
    if (status == HAL_OK)
      status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status =
          write_text(writer, "status", jh_span(hal_status_to_string(read)));
    if (status == HAL_OK)
      status = write_string(writer, "message", &error.message);
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  } else if (status == HAL_OK) {
    status = write_text(writer, "integerState",
                        jh_span(value.kind == JH_CONFIG_INTEGER_VALUE ? "value"
                                : value.kind == JH_CONFIG_INTEGER_OUT_OF_RANGE
                                    ? "outOfRange"
                                    : "none"));
    if (status == HAL_OK && value.kind == JH_CONFIG_INTEGER_VALUE) {
      status = write_key(writer, "integer");
      if (status == HAL_OK)
        status = jb_json_writer_i64(writer, value.integer);
    }
  }
  if (status == HAL_OK)
    status = write_list(writer, "unresolved", &value.unresolved);
  if (status == HAL_OK)
    status = jb_json_writer_object_end(writer);
  jh_failure_destroy(&error);
  jh_config_value_destroy(&value);
  return status;
}

static hal_status_t write_result(jb_json_writer_t *writer,
                                 const jh_project_config_t *config,
                                 const char *header, const char *preinclude,
                                 jh_failure_t *failure) {
  hal_status_t status = jb_json_writer_object_begin(writer);
  if (status == HAL_OK)
    status = write_count(writer, "formatVersion", 1);
  if (status == HAL_OK)
    status = write_text(writer, "command", jh_span("config.inspect"));
  if (status == HAL_OK)
    status = write_text(writer, "status", jh_span("HAL_OK"));
  if (status == HAL_OK)
    status = write_text(writer, "header", jh_span(header));
  if (status == HAL_OK)
    status = write_text(writer, "preinclude", jh_span(preinclude));
  if (status == HAL_OK)
    status = write_list(writer, "files", &config->files);
  if (status == HAL_OK)
    status = write_list(writer, "targets", &config->targets);
  if (status == HAL_OK)
    status = open_array(writer, "variants");
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&config->variants);
       ++i) {
    const jh_config_variant_t *item = jb_vector_const_at(&config->variants, i);
    status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status = write_string(writer, "id", &item->id);
    if (status == HAL_OK)
      status = write_string(writer, "description", &item->description);
    if (status == HAL_OK)
      status = write_list(writer, "definitions", &item->definitions);
    if (status == HAL_OK)
      status = write_string(writer, "source", &item->source);
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  }
  if (status == HAL_OK)
    status = jb_json_writer_array_end(writer);
  if (status == HAL_OK)
    status = open_array(writer, "macros");
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&config->macros);
       ++i)
    status = write_macro(writer, config, macro_at(config, i), failure);
  if (status == HAL_OK)
    status = jb_json_writer_array_end(writer);
  if (status == HAL_OK)
    status = open_array(writer, "features");
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&config->macros);
       ++i)
    if (jh_config_macro_is_feature(macro_at(config, i)))
      status = jb_json_writer_string(
          writer, jh_string_span(&macro_at(config, i)->name));
  if (status == HAL_OK)
    status = jb_json_writer_array_end(writer);
  if (status == HAL_OK)
    status = open_array(writer, "dependencies");
  for (size_t i = 0;
       status == HAL_OK && i < jb_vector_size(&config->dependencies); ++i) {
    const jh_config_dependency_t *item =
        jb_vector_const_at(&config->dependencies, i);
    status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status = write_string(writer, "name", &item->name);
    if (status == HAL_OK)
      status = write_list(writer, "unknown", &item->unknown);
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  }
  if (status == HAL_OK)
    status = jb_json_writer_array_end(writer);
  if (status == HAL_OK)
    status = jh_config_write_directives(writer, &config->directives);
  if (status == HAL_OK)
    status = write_key(writer, "error");
  if (status == HAL_OK && !config->stopped) {
    status = jb_json_writer_null(writer);
  } else if (status == HAL_OK) {
    status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status = write_string(writer, "message", &config->error_message);
    if (status == HAL_OK)
      status = write_string(writer, "source", &config->error_source);
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  }
  return status == HAL_OK ? jb_json_writer_object_end(writer) : status;
}

hal_status_t jh_project_config_json(const jh_project_config_t *config,
                                    const char *header, const char *preinclude,
                                    jb_string_t *out, jh_failure_t *failure) {
  if (config == NULL || header == NULL || preinclude == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  failure_reset(failure);
  jb_string_clear(out);
  // The envelope holds arrays of objects with one array or object inside;
  // every object has a few short keys.
  const jb_json_writer_limits_t limits = {SIZE_MAX / 2, 8, 64, 4096,
                                          SIZE_MAX / 2};
  jb_vector_t scratch = {0};
  size_t bytes = 0, written = 0;
  hal_status_t status = jb_vector_init(&scratch, 1, heap());
  if (status == HAL_OK)
    status = jb_json_writer_workspace_size(&limits, &bytes);
  if (status == HAL_OK)
    status = jb_vector_resize(&scratch, bytes);
  jb_json_writer_t writer = {NULL, HAL_NONE};
  const jb_json_workspace_t work = {jb_vector_data(&scratch),
                                    jb_vector_size(&scratch)};
  if (status == HAL_OK)
    status = jb_json_writer_init(&writer, &limits, work, jh_string_sink, out);
  if (status == HAL_OK)
    status = write_result(&writer, config, header, preinclude, failure);
  if (status == HAL_OK)
    status = jb_json_writer_finish(&writer, &written);
  if (status == HAL_OK)
    status = jb_string_append(out, "\n", 1);
  jb_vector_destroy(&scratch);
  return finish(failure, status);
}

hal_status_t jh_project_config_text(const jh_project_config_t *config,
                                    jb_string_t *out) {
  hal_status_t status = HAL_OK;
  jb_string_clear(out);
  for (size_t i = 0; i < jb_vector_size(&config->macros); ++i) {
    const jh_config_macro_t *item = macro_at(config, i);
    jh_put(out, &status, "#define ");
    jh_put_span(out, &status, jh_string_span(&item->name));
    if (item->function_like) {
      jh_put(out, &status, "(");
      for (size_t j = 0; j < jb_vector_size(&item->parameters); ++j) {
        jh_put(out, &status, j != 0 ? "," : "");
        jh_put_span(out, &status,
                    jh_string_span(jh_list_at(&item->parameters, j)));
      }
      jh_put(out, &status, ")");
    }
    if (jb_string_size(&item->body) != 0) {
      jh_put(out, &status, " ");
      jh_put_span(out, &status, jh_string_span(&item->body));
    }
    jh_put(out, &status, "\n");
  }
  return status;
}

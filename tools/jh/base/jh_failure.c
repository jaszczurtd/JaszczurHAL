#include "jh_failure.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void jh_failure_init(jh_failure_t *failure) {
  const jh_failure_t empty = {HAL_OK, 0, {0}};
  *failure = empty;
  // Initialization only checks its arguments, which are valid here.
  (void)jb_string_init(&failure->message, jb_heap_allocator());
}

void jh_failure_destroy(jh_failure_t *failure) {
  const jh_failure_t empty = {HAL_NONE, 0, {0}};
  jb_string_destroy(&failure->message);
  *failure = empty;
}

hal_status_t jh_fail(jh_failure_t *failure, hal_status_t status, int exit_code,
                     const jb_json_span_t *parts, size_t count) {
  // Parts may quote the previous message: the new one is built aside.
  jb_string_t message = {0};
  hal_status_t stored = jb_string_init(&message, jb_heap_allocator());
  for (size_t index = 0; stored == HAL_OK && index < count; ++index)
    if (parts[index].length != 0)
      stored =
          jb_string_append(&message, parts[index].data, parts[index].length);
  if (stored != HAL_OK) {
    jb_string_destroy(&message);
    return jh_fail_storage(failure, stored);
  }
  jb_string_destroy(&failure->message);
  failure->message = message;
  failure->status = status;
  failure->exit_code = exit_code;
  return status;
}

hal_status_t jh_fail_text(jh_failure_t *failure, hal_status_t status,
                          int exit_code, const char *message) {
  const jb_json_span_t part = jh_span(message);
  return jh_fail(failure, status, exit_code, &part, 1);
}

hal_status_t jh_fail_storage(jh_failure_t *failure, hal_status_t status) {
  if (status == HAL_OK)
    return HAL_OK;
  jb_string_clear(&failure->message);
  failure->status = status;
  failure->exit_code = 4;
  return status;
}

hal_status_t jh_fail_arguments(jh_failure_t *failure) {
  return failure == NULL ? HAL_EINVAL
                         : jh_fail_text(failure, HAL_EINVAL, 3,
                                        "missing required argument");
}

hal_status_t jh_require(jh_failure_t *failure, hal_status_t status,
                        int exit_code, const char *message) {
  return status == HAL_OK ? HAL_OK
                          : jh_fail_text(failure, status, exit_code, message);
}

jb_json_span_t jh_span(const char *text) {
  const jb_json_span_t result = {text, strlen(text)};
  return result;
}

bool jh_span_equal(jb_json_span_t a, jb_json_span_t b) {
  return a.length == b.length &&
         (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

jb_json_span_t jh_string_span(const jb_string_t *text) {
  const jb_json_span_t span = {jb_string_data(text), jb_string_size(text)};
  return span;
}

hal_status_t jh_append(jb_string_t *out, const char *text) {
  return jb_string_append(out, text, strlen(text));
}

hal_status_t jh_append_span(jb_string_t *out, jb_json_span_t text) {
  return text.length == 0 ? HAL_OK
                          : jb_string_append(out, text.data, text.length);
}

void jh_put(jb_string_t *out, hal_status_t *status, const char *text) {
  if (*status == HAL_OK)
    *status = jh_append(out, text);
}

void jh_put_span(jb_string_t *out, hal_status_t *status, jb_json_span_t text) {
  if (*status == HAL_OK)
    *status = jh_append_span(out, text);
}

hal_status_t jh_join(jb_string_t *out, const jb_json_span_t *parts,
                     size_t count) {
  jb_string_clear(out);
  hal_status_t status = HAL_OK;
  for (size_t i = 0; i < count; ++i)
    jh_put_span(out, &status, parts[i]);
  return status;
}

hal_status_t jh_string_sink(void *user, const char *data, size_t length) {
  return jb_string_append(user, data, length);
}

void jh_put_int64(jb_string_t *out, hal_status_t *status, int64_t value) {
  char number[24];
  const int written = snprintf(number, sizeof number, "%" PRId64, value);
  if (*status == HAL_OK)
    *status = jb_string_append(out, number, (size_t)written);
}

int jh_span_compare(jb_json_span_t a, jb_json_span_t b) {
  const size_t shorter = a.length < b.length ? a.length : b.length;
  const int order = shorter == 0 ? 0 : memcmp(a.data, b.data, shorter);
  if (order != 0)
    return order;
  return a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
}

jb_json_span_t jh_span_path_stem(jb_json_span_t path) {
  size_t begin = path.length;
  while (begin > 0 && path.data[begin - 1] != '/' &&
         path.data[begin - 1] != '\\')
    --begin;
  size_t end = path.length;
  for (size_t at = path.length; at > begin + 1; --at)
    if (path.data[at - 1] == '.') {
      end = at - 1;
      break;
    }
  const jb_json_span_t stem = {path.data + begin, end - begin};
  return stem;
}

hal_status_t jh_list_init(jb_vector_t *list) {
  return jb_vector_init(list, sizeof(jb_string_t), jb_heap_allocator());
}

const jb_string_t *jh_list_at(const jb_vector_t *list, size_t index) {
  return jb_vector_const_at(list, index);
}

hal_status_t jh_list_push(jb_vector_t *list, jb_json_span_t text) {
  return jh_list_push_parts(list, &text, 1);
}

hal_status_t jh_list_push_parts(jb_vector_t *list, const jb_json_span_t *parts,
                                size_t count) {
  jb_string_t item = {0};
  hal_status_t status = jb_string_init(&item, jb_heap_allocator());
  for (size_t i = 0; status == HAL_OK && i < count; ++i)
    if (parts[i].length != 0)
      status = jb_string_append(&item, parts[i].data, parts[i].length);
  if (status == HAL_OK)
    status = jb_vector_append(list, &item, 1);
  if (status != HAL_OK)
    jb_string_destroy(&item);
  return status;
}

static int compare_strings(const void *left, const void *right) {
  return jh_span_compare(jh_string_span(left), jh_string_span(right));
}

void jh_list_sort(jb_vector_t *list) {
  if (jb_vector_size(list) > 1)
    qsort(jb_vector_data(list), jb_vector_size(list), sizeof(jb_string_t),
          compare_strings);
}

bool jh_list_contains(const jb_vector_t *list, jb_json_span_t text) {
  for (size_t i = 0; i < jb_vector_size(list); ++i)
    if (jh_span_compare(jh_string_span(jh_list_at(list, i)), text) == 0)
      return true;
  return false;
}

void jh_list_destroy(jb_vector_t *list) {
  for (size_t i = 0; i < jb_vector_size(list); ++i)
    jb_string_destroy(jb_vector_at(list, i));
  jb_vector_destroy(list);
}

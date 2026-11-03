#include "jh_failure.h"
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

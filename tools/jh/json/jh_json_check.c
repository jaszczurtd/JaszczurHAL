#include "jh_json_check.h"
#include "jh_python_repr.h"
#include <jb/array.h>
#include <stdio.h>
#include <string.h>

hal_status_t jh_check_fail(jh_failure_t *failure, const jb_json_span_t *parts,
                           size_t count) {
  return jh_fail(failure, HAL_ECONFIG, 2, parts, count);
}

hal_status_t jh_check_got(jh_failure_t *failure, jb_json_span_t path,
                          jb_json_span_t json_path, jb_json_span_t got,
                          const char *expected) {
  const jb_json_span_t parts[] = {
      path, jh_span(": "),          json_path,        jh_span(": got "),
      got,  jh_span("; expected "), jh_span(expected)};
  return jh_check_fail(failure, parts, COUNTOF(parts));
}

hal_status_t jh_check_value(jh_failure_t *failure, jb_json_span_t path,
                            jb_json_span_t json_path,
                            const jb_json_tree_view_t *value,
                            const char *expected) {
  jb_string_t got = {0};
  jb_json_type_t type = JB_JSON_NULL;
  const bool none = value == NULL || jb_json_tree_type(*value, &type) != HAL_OK;
  hal_status_t status = jb_string_init(&got, jb_heap_allocator());
  if (status == HAL_OK)
    status = none ? jh_append(&got, "None")
                  : jh_python_repr_json_append(&got, *value);
  status = status == HAL_OK ? jh_check_got(failure, path, json_path,
                                           jh_string_span(&got), expected)
                            : jh_fail_storage(failure, status);
  jb_string_destroy(&got);
  return status;
}

hal_status_t jh_check_list(jh_failure_t *failure, jb_json_span_t path,
                           jb_json_span_t json_path, const jb_vector_t *list,
                           const char *expected) {
  jb_string_t got = {0};
  hal_status_t status = jb_string_init(&got, jb_heap_allocator());
  if (status == HAL_OK)
    status = jh_repr_list(&got, list);
  status = status == HAL_OK ? jh_check_got(failure, path, json_path,
                                           jh_string_span(&got), expected)
                            : jh_fail_storage(failure, status);
  jb_string_destroy(&got);
  return status;
}

hal_status_t jh_repr_list(jb_string_t *out, const jb_vector_t *list) {
  hal_status_t status = jh_append(out, "[");
  for (size_t index = 0; status == HAL_OK && index < jb_vector_size(list);
       ++index) {
    if (index != 0)
      status = jh_append(out, ", ");
    if (status == HAL_OK)
      status =
          jh_python_repr_append(out, jh_string_span(jh_list_at(list, index)));
  }
  return status == HAL_OK ? jh_append(out, "]") : status;
}

/* The fields the rule names that the object has (unknown false) or lacks. */
static hal_status_t
field_differences(jb_json_tree_view_t value, const char *const *required,
                  size_t required_count, const char *const *allowed,
                  size_t allowed_count, jb_vector_t *missing,
                  jb_vector_t *unknown) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < required_count; ++i) {
    jb_json_tree_view_t found = {0};
    if (!jh_json_member(value, required[i], &found))
      status = jh_list_push(missing, jh_span(required[i]));
  }
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(value); ++i) {
    jb_json_tree_view_t item = {0};
    jb_json_span_t key = {NULL, 0};
    status = jb_json_tree_at(value, i, &item);
    if (status == HAL_OK)
      status = jb_json_tree_key(item, &key);
    bool known = false;
    for (size_t a = 0; status == HAL_OK && a < allowed_count; ++a)
      known = known || jh_span_compare(key, jh_span(allowed[a])) == 0;
    if (status == HAL_OK && !known)
      status = jh_list_push(unknown, key);
  }
  return status;
}

hal_status_t
jh_check_exact_fields(jh_failure_t *failure, jb_json_span_t path,
                      jb_json_span_t json_path, jb_json_tree_view_t value,
                      const char *const *required, size_t required_count,
                      const char *const *allowed, size_t allowed_count) {
  if (jh_json_type(value) != JB_JSON_OBJECT)
    return jh_check_value(failure, path, json_path, &value, "an object");
  jb_vector_t missing = {0}, unknown = {0};
  hal_status_t status = jh_list_init(&missing);
  if (status == HAL_OK)
    status = jh_list_init(&unknown);
  if (status == HAL_OK)
    status = field_differences(value, required, required_count, allowed,
                               allowed_count, &missing, &unknown);
  if (status != HAL_OK) {
    status = jh_fail_storage(failure, status);
  } else if (jb_vector_size(&missing) != 0 || jb_vector_size(&unknown) != 0) {
    const bool absent = jb_vector_size(&missing) != 0;
    const char *const *names = absent ? required : allowed;
    const size_t names_count = absent ? required_count : allowed_count;
    jb_string_t expected = {0};
    jb_vector_t sorted = {0};
    status = jb_string_init(&expected, jb_heap_allocator());
    if (status == HAL_OK)
      status = jh_list_init(&sorted);
    for (size_t i = 0; status == HAL_OK && i < names_count; ++i)
      status = jh_list_push(&sorted, jh_span(names[i]));
    jh_list_sort(&sorted);
    jh_list_sort(absent ? &missing : &unknown);
    if (status == HAL_OK)
      status =
          jh_append(&expected, absent ? "required fields " : "only fields ");
    if (status == HAL_OK)
      status = jh_repr_list(&expected, &sorted);
    status = status == HAL_OK ? jh_check_list(failure, path, json_path,
                                              absent ? &missing : &unknown,
                                              jb_string_data(&expected))
                              : jh_fail_storage(failure, status);
    jh_list_destroy(&sorted);
    jb_string_destroy(&expected);
  }
  jh_list_destroy(&unknown);
  jh_list_destroy(&missing);
  return status;
}

hal_status_t jh_check_json_error(jh_failure_t *failure, jb_json_span_t path,
                                 const jh_json_location_t *where,
                                 const jh_failure_t *reason) {
  if (reason->status == HAL_ENOMEM)
    return jh_fail_storage(failure, HAL_ENOMEM);
  char place[64] = "";
  if (where->located)
    (void)snprintf(place, sizeof place, " (line %zu, column %zu)", where->line,
                   where->column);
  const jb_json_span_t parts[] = {path, jh_span(": invalid JSON: "),
                                  jh_string_span(&reason->message),
                                  jh_span(place)};
  return jh_check_fail(failure, parts, COUNTOF(parts));
}

hal_status_t jh_check_load_object(const char *path, size_t limit,
                                  jb_json_tree_t **tree,
                                  jb_json_tree_view_t *root,
                                  jh_failure_t *failure) {
  if (path == NULL || tree == NULL || root == NULL)
    return jh_fail_arguments(failure);
  *tree = NULL;
  jh_json_location_t where;
  jh_failure_t reason;
  jh_json_location_init(&where);
  jh_failure_init(&reason);
  hal_status_t status = jh_json_read_value(&where, path, limit, tree, &reason);
  if (status != HAL_OK)
    status = jh_check_json_error(failure, jh_span(path), &where, &reason);
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jb_json_tree_root(*tree, root));
  if (status == HAL_OK && jh_json_type(*root) != JB_JSON_OBJECT)
    status =
        jh_check_value(failure, jh_span(path), jh_span("$"), root, "an object");
  jh_failure_destroy(&reason);
  jh_json_location_destroy(&where);
  return status;
}

bool jh_json_string(jb_json_tree_view_t value, jb_json_span_t *text) {
  return jh_json_type(value) == JB_JSON_STRING &&
         jb_json_tree_text(value, text) == HAL_OK;
}

size_t jh_json_count(jb_json_tree_view_t value) {
  size_t count = 0;
  (void)jb_json_tree_count(value, &count);
  return count;
}

hal_status_t jh_json_path_join(jb_string_t *out, jb_json_span_t base,
                               const char *suffix) {
  jb_string_clear(out);
  hal_status_t status = jh_append_span(out, base);
  return status == HAL_OK ? jh_append(out, suffix) : status;
}

hal_status_t jh_json_path_index(jb_string_t *out, jb_json_span_t base,
                                size_t index) {
  char number[32];
  const int written = snprintf(number, sizeof number, "[%zu]", index);
  jb_string_clear(out);
  hal_status_t status = jh_append_span(out, base);
  return status == HAL_OK ? jb_string_append(out, number, (size_t)written)
                          : status;
}

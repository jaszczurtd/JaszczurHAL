#include "jh_json_schema.h"

/* C permits this numeric status. Producing it by a C++ enum conversion would
 * itself be undefined, so the ABI regression needs a C translation unit.
 */
hal_status_t
jh_schema_test_unknown_status(void *user, const jh_json_schema_error_t *error) {
  (void)user;
  (void)error;
  return (hal_status_t)777;
}

hal_status_t jh_schema_test_unknown_load(void *user, jb_json_span_t base,
                                         jb_json_span_t reference,
                                         jh_json_schema_source_t *out) {
  (void)user;
  (void)base;
  (void)reference;
  (void)out;
  return (hal_status_t)777;
}

#ifndef JH_CATALOGUE_INTERNAL_H
#define JH_CATALOGUE_INTERNAL_H

/* Borrowed nodes of a schema-checked binding. Decoded text is kept in the
 * reader until destruction. The first failed operation is retained; later
 * reads do nothing. Callers check status before publishing a result. */
#include "jh_type_catalogue.h"

typedef struct {
  jb_vector_t strings;
  hal_status_t status;
  jh_failure_t *failure;
} jh_node_reader_t;

void jh_node_reader_init(jh_node_reader_t *reader, jh_failure_t *failure);
void jh_node_reader_destroy(jh_node_reader_t *reader);
void jh_node_require(jh_node_reader_t *reader, hal_status_t status,
                     int exit_code, const char *message);
void jh_node_storage(jh_node_reader_t *reader, hal_status_t status);
jb_json_node_t jh_binding_root(jh_node_reader_t *reader,
                               const jh_binding_t *binding);
jb_json_type_t jh_node_type(jh_node_reader_t *reader, jb_json_node_t node);
bool jh_node_find(jh_node_reader_t *reader, jb_json_node_t node,
                  const char *name, jb_json_node_t *out);
jb_json_node_t jh_node_member(jh_node_reader_t *reader, jb_json_node_t node,
                              const char *name);
const char *jh_node_text(jh_node_reader_t *reader, jb_json_node_t node,
                         bool key);
/* Decoded bytes, retaining embedded NULs. Use for arbitrary property values. */
jb_json_span_t jh_node_string(jh_node_reader_t *reader, jb_json_node_t node,
                              bool key);
/* Joined text owned by the reader; empty after a recorded failure. */
const char *jh_node_join(jh_node_reader_t *reader, const jb_json_span_t *parts,
                         size_t count);
const char *jh_node_index(jh_node_reader_t *reader, size_t index);
jb_json_span_t jh_node_raw(jh_node_reader_t *reader, jb_json_node_t node);
bool jh_node_bool(jh_node_reader_t *reader, jb_json_node_t node);
size_t jh_node_count(jh_node_reader_t *reader, jb_json_node_t node);
bool jh_node_first(jh_node_reader_t *reader, jb_json_node_t node,
                   jb_json_node_t *out);
bool jh_node_next(jh_node_reader_t *reader, jb_json_node_t *node);
/* Compare borrowed UTF-8 spans used as ordered-map keys. */
int jh_catalogue_span_order(const void *left, const void *right, void *user);
/* Format the hardware diagnostic unless an earlier operation failed. */
void jh_catalogue_fail(jh_node_reader_t *reader, const char *code,
                       const char *source, const char *pointer,
                       const jb_json_span_t *parts, size_t count);
#endif

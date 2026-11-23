#ifndef JH_ESP_INTERNAL_H
#define JH_ESP_INTERNAL_H

/* Shared state of one ESP-IDF backend invocation (jh_esp_build.h). Every
 * helper records the first failure in e->failure, with the message of the
 * Python runner, and returns without side effects once one is recorded. */
#include "jh_board_registry.h"
#include "jh_esp_build.h"
#include "jh_feature_registry.h"
#include "jh_hardware.h"
#include <jb/file.h>
#include <jb/json_writer.h>
#include <stdio.h>

#define JH_ESP_GENERATED_DIR "generated/jaszczurhal"
#define JH_ESP_MAX_BYTES (64u * 1024u * 1024u)

/* The build model of resolve_build_model. Paths are canonical; lists hold
 * jb_string_t. */
typedef struct {
  const char *project_dir, *config_dir;
  const char *target, *board, *idf_target, *project_name;
  const jh_board_descriptor_t *target_descriptor, *board_descriptor;
  jb_vector_t project_sources, project_include_dirs, extra_include_dirs,
      link_libraries;
  jb_vector_t requested, resolved;
  jb_vector_t integration_sources, component_dependencies, private_dependencies;
  jb_vector_t compile_definitions;
  jb_vector_t managed_frameworks, component_include_dirs;
  const char
      *ram_code_sources; /* NULL when HAL_RAM_CODE_SOURCES is undefined. */
  bool device_tree;
  /* A device tree build: its resolution, header, the -D requests finalize
   * reads the program configuration with and the variant (NULL: none). */
  const char *assembly, *document, *header, *variant;
  jb_vector_t tree_definitions;
  const char *build_key;
} jh_esp_model_t;

typedef struct {
  const jh_esp_options_t *options;
  jh_failure_t *failure;
  jb_vector_t strings; /* Owned texts every helper returns. */
  FILE *log;           /* Progress and child stdout. */
  const char *repo_root;
  jh_board_registry_t boards;
  jh_feature_registry_t features;
  jh_hardware_registry_t hardware;
  bool hardware_loaded;
  jb_json_tree_t *tables; /* config/tooling/esp_idf_components.json */
  jb_json_tree_view_t tables_root;
  jh_esp_model_t model;
  const char *build_dir;
  jb_vector_t environment; /* Exported ESP-IDF environment, "K=V". */
  bool exported;
  const char *idf_dir, *idf_version, *idf_commit;
} jh_esp_t;

/* ---- Support (jh_esp_support.c) ---- */
bool esp_ok(const jh_esp_t *e);
bool esp_storage(jh_esp_t *e, hal_status_t status);
/* The runner's EspIdfError: HAL_ECONFIG with the concatenated message. */
bool esp_fail(jh_esp_t *e, const jb_json_span_t *parts, size_t count);
const char *esp_text(jh_esp_t *e, const jb_json_span_t *parts, size_t count);
#define ESP_TEXT(e, ...) esp_text((e), JH_PARTS(__VA_ARGS__))
#define ESP_FAIL(e, ...) esp_fail((e), JH_PARTS(__VA_ARGS__))
/* repr() of a Python str, or of the Python value of a JSON tree. */
jb_json_span_t esp_repr(jh_esp_t *e, jb_json_span_t text);
jb_json_span_t esp_repr_tree(jh_esp_t *e, jb_json_tree_view_t value);
/* repr() of a list of strings. */
jb_json_span_t esp_repr_list(jh_esp_t *e, const jb_vector_t *list);
const char *esp_decimal(jh_esp_t *e, uint64_t value);
const char *esp_hex(jh_esp_t *e, uint64_t value);
bool esp_list_init(jh_esp_t *e, jb_vector_t *list);
bool esp_push(jh_esp_t *e, jb_vector_t *list, jb_json_span_t text);
/* Append text unless the list holds it (dict.fromkeys order). */
bool esp_push_unique(jh_esp_t *e, jb_vector_t *list, jb_json_span_t text);
const char *esp_at(const jb_vector_t *list, size_t index);
const char *esp_join_list(jh_esp_t *e, const jb_vector_t *list,
                          const char *separator);

/* Paths: Path.resolve() (strict=False) against base, the native and
 * as_posix() forms of path joins, and the relative_to() checks. */
const char *esp_expand_user(jh_esp_t *e, const char *path);
bool esp_absolute(const char *path);
const char *esp_resolve(jh_esp_t *e, const char *path, const char *base);
const char *esp_join(jh_esp_t *e, const char *base, const char *name);
const char *esp_posix(jh_esp_t *e, const char *path);
bool esp_inside(const char *path, const char *parent);
/* as_posix() of path relative to parent (both canonical); NULL outside. */
const char *esp_relative(jh_esp_t *e, const char *path, const char *parent);
const char *esp_parent(jh_esp_t *e, const char *path);
const char *esp_name(const char *path);
bool esp_is_file(const char *path);
bool esp_is_dir(const char *path);
bool esp_exists(const char *path);
bool esp_file_size(const char *path, uint64_t *size);

/* Files: bounded reads, write_text() publication, SHA-256. */
bool esp_read(jh_esp_t *e, const char *path, jb_file_data_t *out);
bool esp_write_text(jh_esp_t *e, const char *path, jb_json_span_t content);
bool esp_sha256_file(jh_esp_t *e, const char *path, char out[65]);
bool esp_remove(jh_esp_t *e, const char *path);

/* JSON: json.loads of a file (NULL with a failure), the object check of
 * _load_object, and json.dumps(indent=2, sort_keys=True) + "\n" of compact
 * JSON text. */
jb_json_tree_t *esp_load_json(jh_esp_t *e, const char *path, bool object,
                              jb_json_tree_view_t *root);
const char *esp_dumps(jh_esp_t *e, jb_json_span_t compact);
/* A tree of strict JSON text (any nesting up to 64). NULL with a failure. */
jb_json_tree_t *esp_parse_text(jh_esp_t *e, jb_json_span_t text,
                               jb_json_tree_view_t *root);
jb_json_span_t esp_field_text(jb_json_tree_view_t object, const char *key,
                              bool *is_text);
bool esp_tree_equal(jh_esp_t *e, jb_json_tree_view_t a, jb_json_tree_view_t b);

/* A compact JSON document built with jb_json_writer into a string. */
typedef struct {
  jb_json_writer_t writer;
  jb_vector_t workspace;
  jb_string_t text;
  hal_status_t status;
} esp_json_t;
bool esp_json_begin(jh_esp_t *e, esp_json_t *json);
void esp_json_end(esp_json_t *json);
void esp_json_key(esp_json_t *json, const char *key);
void esp_json_string(esp_json_t *json, const char *key, jb_json_span_t value);
void esp_json_u64(esp_json_t *json, const char *key, uint64_t value);
void esp_json_null(esp_json_t *json, const char *key);
void esp_json_list(esp_json_t *json, const char *key, const jb_vector_t *list);
void esp_json_tree(esp_json_t *json, const char *key,
                   jb_json_tree_view_t value);
void esp_json_object(esp_json_t *json, const char *key);
void esp_json_object_end(esp_json_t *json);
void esp_json_array(esp_json_t *json, const char *key);
void esp_json_array_end(esp_json_t *json);

/* Regular expressions with the str semantics of Python's re (PCRE2 with
 * UTF and UCP): the first match of pattern in subject and its group 1.
 * Returns false without a match; a pattern PCRE2 rejects is a failure. */
bool esp_search(jh_esp_t *e, const char *pattern, jb_json_span_t subject,
                bool caseless, jb_json_span_t *group, size_t *groups);

/* Environments: "K=V" lists of jb_string_t. */
bool esp_environment_capture(jh_esp_t *e, jb_vector_t *environment);
const char *esp_environment_get(const jb_vector_t *environment,
                                const char *name);
bool esp_environment_set(jh_esp_t *e, jb_vector_t *environment,
                         const char *name, const char *value);
/* An executable named name found in the PATH of environment. */
const char *esp_find_program(jh_esp_t *e, const jb_vector_t *environment,
                             const char *name);
const char *esp_host_python(jh_esp_t *e);

/* Child processes through jb_process_run. */
enum {
  ESP_RUN_FORWARD = 0, /* stdout to the log stream, stderr to stderr. */
  ESP_RUN_CAPTURE = 1, /* stdout to out, stderr to err (separately). */
  ESP_RUN_MERGE = 2,   /* Both into out, as stderr=STDOUT; echoed when
                          echo is set. */
};
typedef struct {
  const char *program;
  const jb_vector_t *arguments;
  const char *directory;          /* NULL: base. */
  const jb_vector_t *environment; /* NULL inherits. */
  int mode;
  bool echo, text; /* text: universal newlines and errors="replace". */
  uint32_t timeout_ms;
  jb_string_t *out, *err;
} esp_process_t;
/* HAL_OK with exit_code set when the process ran; another status when it
 * could not start, timed out or was canceled (not recorded as a failure,
 * except cancellation and memory). */
hal_status_t esp_process(jh_esp_t *e, const esp_process_t *process,
                         int *exit_code);
/* Text of captured output as Python's text mode decodes it. */
bool esp_decode_text(jh_esp_t *e, jb_json_span_t input, jb_string_t *out);
/* str.strip() and str.splitlines()[0] of text. */
jb_json_span_t esp_strip(jb_json_span_t text);
jb_json_span_t esp_first_line(jb_json_span_t text);

/* ---- Model (jh_esp_model.c) ---- */
bool esp_load_tables(jh_esp_t *e);
jb_json_tree_view_t esp_table(jh_esp_t *e, const char *key);
void esp_model_init(jh_esp_model_t *model);
void esp_model_destroy(jh_esp_model_t *model);
bool esp_resolve_project_dir(jh_esp_t *e, const char **out);
bool esp_project_name(jh_esp_t *e, const char *project_dir, const char **out);
bool esp_resolve_model(jh_esp_t *e, const char *project_dir,
                       const char *project_name);
bool esp_resolve_build_dir(jh_esp_t *e, const char *project_dir);
const char *esp_sdkconfig_defaults_text(jh_esp_t *e);
bool esp_materialize(jh_esp_t *e);
/* generate_board_inputs into directory with the HAL compile command
 * (compiler: jb_string_t list, empty for none). */
bool esp_board_inputs(jh_esp_t *e, const char *directory,
                      const jb_vector_t *compiler);
/* abi_compile_command: the HAL component's compile command of the
 * configured build; fails when compile_commands.json has none. */
bool esp_abi_command(jh_esp_t *e, jb_vector_t *command);
bool esp_validate_board_contract(jh_esp_t *e);
/* validate_generated_project_contract; project receives the loaded
 * jh_esp_idf_project.json. */
jb_json_tree_t *esp_validate_project_contract(jh_esp_t *e,
                                              jb_json_tree_view_t *project);
/* The sdkconfig.defaults of the build: generated, then the project's. */
bool esp_sdkconfig_defaults(jh_esp_t *e, jb_vector_t *paths);

/* ---- Artifacts (jh_esp_artifacts.c) ---- */
bool esp_collect_toolchain(jh_esp_t *e);
bool esp_validate_artifacts(jh_esp_t *e, size_t *flash_images);
/* _idf_environment of environment (NULL: this process). */
bool esp_idf_environment(jh_esp_t *e, const jb_vector_t *environment,
                         jb_vector_t *out);
const char *esp_idf_python(jh_esp_t *e);

#endif

#ifndef JH_COMPILE_INTERNAL_H
#define JH_COMPILE_INTERNAL_H

/* Shared state of one jh compile invocation (jh_compile.h). The model mirrors
 * the dictionary jh-vscode builds: every helper works on owned JSON trees,
 * keeps member order the way a Python dict does and records the first
 * failure in c->e.failure through the support layer of the ESP-IDF backend
 * (strings, paths, JSON and environment helpers of jh_esp_internal.h). */
#include "jh_compile.h"
#include "jh_esp_internal.h"

typedef struct {
  jh_esp_t e;                   /* Support layer; e.options = &esp_options. */
  jh_esp_options_t esp_options; /* base and root for the support helpers. */
  const jh_compile_options_t *options;
  const char *root;         /* Resolved HAL checkout (jaszczurhal_root). */
  const char *base;         /* Working directory of the command. */
  const char *project_dir;  /* Path(--project).expanduser().resolve(). */
  jb_json_tree_t *registry; /* tooling_target_registry, target -> view. */
  jb_json_tree_t *sources;  /* config["_sources"]. */
  jb_json_tree_t *host;     /* host-environment.json ({} off Windows). */
  jh_feature_registry_t features;
  bool features_loaded;
  /* The evaluated project configuration of the build (project_build). */
  jb_vector_t resolved_features; /* jb_string_t */
  bool device_tree;
  /* What the build preflight reads: whether the evaluated configuration
   * defines the three CYW43 gSPI/lwIP symbols, and the components of a
   * device tree assembly (jb_string_t). */
  bool cyw43_defined;
  jb_vector_t assembly_components;
} jh_compile_ctx_t;

/* ---- Trees (jh_compile_model.c) ---- */
bool cc_ok(const jh_compile_ctx_t *c);
const char *cc_text(jh_compile_ctx_t *c, const jb_json_span_t *parts,
                    size_t count);
#define CC_TEXT(c, ...) cc_text((c), JH_PARTS(__VA_ARGS__))
/* A writable copy of text owned by c, or NULL with a failure. */
char *cc_mutable(jh_compile_ctx_t *c, const char *text);
/* A configuration error (exit code 2) with the concatenated message. */
bool cc_fail(jh_compile_ctx_t *c, const jb_json_span_t *parts, size_t count);
#define CC_FAIL(c, ...) cc_fail((c), JH_PARTS(__VA_ARGS__))
/* A build jh-vscode does not implement: exit code 8, its EXIT_UNSUPPORTED. */
bool cc_unsupported(jh_compile_ctx_t *c, const jb_json_span_t *parts,
                    size_t count);
#define CC_UNSUPPORTED(c, ...) cc_unsupported((c), JH_PARTS(__VA_ARGS__))
bool cc_storage(jh_compile_ctx_t *c, hal_status_t status);

jb_json_tree_t *cc_object(jh_compile_ctx_t *c);
jb_json_tree_t *cc_array(jh_compile_ctx_t *c);
jb_json_tree_t *cc_string(jh_compile_ctx_t *c, const char *text);
jb_json_tree_t *cc_bool(jh_compile_ctx_t *c, bool value);
jb_json_tree_t *cc_copy(jh_compile_ctx_t *c, jb_json_tree_view_t value);
jb_json_tree_view_t cc_root(const jb_json_tree_t *tree);
/* value of a member, or a view without a tree. */
jb_json_tree_view_t cc_get(jb_json_tree_view_t object, const char *key);
bool cc_has(jb_json_tree_view_t object, const char *key);
/* Python truthiness of a value; a missing member is false. */
bool cc_truthy(jb_json_tree_view_t value);
bool cc_is(jb_json_tree_view_t value, jb_json_type_t type);
/* The string of a string value, or NULL. */
const char *cc_str(jh_compile_ctx_t *c, jb_json_tree_view_t value);
/* str(value): the text of a string, repr() of anything else. */
const char *cc_pystr(jh_compile_ctx_t *c, jb_json_tree_view_t value);
/* Members of a root object: set (copy), set text, erase, deep merge. */
void cc_set(jh_compile_ctx_t *c, jb_json_tree_t *tree, const char *key,
            jb_json_tree_view_t value);
void cc_set_text(jh_compile_ctx_t *c, jb_json_tree_t *tree, const char *key,
                 const char *text);
void cc_erase(jh_compile_ctx_t *c, jb_json_tree_t *tree, const char *key);
void cc_merge(jh_compile_ctx_t *c, jb_json_tree_t *tree,
              jb_json_tree_view_t overlay);
void cc_append(jh_compile_ctx_t *c, jb_json_tree_t *array,
               jb_json_tree_view_t value);
void cc_append_text(jh_compile_ctx_t *c, jb_json_tree_t *array,
                    const char *text);
/* A copy of the object member key of tree, or a new empty object when it is
 * missing or not an object (dict(config.get(key) or {})). */
jb_json_tree_t *cc_section(jh_compile_ctx_t *c, jb_json_tree_view_t object,
                           const char *key);
/* sources[key] = text. */
void cc_source(jh_compile_ctx_t *c, const char *key, const char *text);

/* Paths: str(PurePath(text)) and os.path.join of the host. */
const char *cc_purepath(jh_compile_ctx_t *c, const char *text);
const char *cc_os_join(jh_compile_ctx_t *c, const char *base, const char *name);
/* Path(path) / name, rendered as str(). */
const char *cc_path_join(jh_compile_ctx_t *c, const char *base,
                         const char *name);
bool cc_path_within(jh_compile_ctx_t *c, const char *child, const char *parent);
/* Equality of two resolved paths: case-insensitive on Windows, as Path. */
bool cc_same_path(const char *a, const char *b);
const char *cc_project_absolute(jh_compile_ctx_t *c, const char *path);

/* ---- Model (jh_compile_model.c) ---- */
/* target_display_name of the model: "<target>/<assembly or board>". */
const char *cc_display_name(jh_compile_ctx_t *c, jb_json_tree_view_t config);
/* load_config_for_action: the model of the build in *config. */
bool cc_model(jh_compile_ctx_t *c, jb_json_tree_t **config);
const char *cc_json_dumps(jh_compile_ctx_t *c, jb_json_tree_view_t value);

/* ---- Plan (jh_compile_plan.c) ---- */
bool cc_plan(jh_compile_ctx_t *c, jb_json_tree_t *config,
             jb_json_tree_t **plan);
/* os.environ.get(name): the value of a variable of this process, or NULL. */
const char *cc_environment(jh_compile_ctx_t *c, const char *name);

/* ---- Memory map overview (jh_compile_memory.c) ---- */
/* format_size of jh-vscode: B below 1 KiB, KiB with one decimal below 1 MiB,
 * else MiB with two. */
const char *cc_format_size(jh_compile_ctx_t *c, uint64_t size);
/* Whether literal starts at text[at]. */
bool cc_literal_at(jb_json_span_t text, size_t at, const char *literal);
/* The sections of <tree>/firmware.elf by memory region, read with objdump -h
 * (NULL: none found, a warning), on the log stream; nothing when
 * JH_VSCODE_MEMORY_OVERVIEW is 0, false, no or off or the ELF is missing. A
 * failing objdump is a warning; false only for memory, log or cancellation
 * failures. */
bool cc_memory_overview(jh_compile_ctx_t *c, const char *tree,
                        const char *objdump);

#endif

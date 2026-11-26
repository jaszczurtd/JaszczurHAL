/* The project model of jh compile: load_config_for_action of
 * vscode/runtime/jh_vscode.py over owned JSON trees (jh_compile_internal.h).
 * C11. */
#include "jh_build_support.h"
#include "jh_compile_internal.h"
#include "jh_feature_lint.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include "jh_process_support.h"
#include "jh_project_build.h"
#include "jh_project_config.h"
#include "jh_python_repr.h"
#include "jh_usb_identifier.h"
#include <jb/filesystem.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_LABEL ".vscode/jaszczurhal.project.json"
#define LOCAL_LABEL ".vscode/jaszczurhal.local.json"
#define SETTINGS_LABEL ".vscode/settings.json"
#define FILE_LIMIT (16u * 1024u * 1024u)

static const jb_json_tree_limits_t tree_limits = {1u << 20, 128, 64u << 20,
                                                  64u << 20};

/* ---- Trees ---- */

bool cc_ok(const jh_compile_ctx_t *c) { return esp_ok(&c->e); }

const char *cc_text(jh_compile_ctx_t *c, const jb_json_span_t *parts,
                    size_t count) {
  return esp_text(&c->e, parts, count);
}

char *cc_mutable(jh_compile_ctx_t *c, const char *text) {
  (void)CC_TEXT(c, jh_span(text));
  if (!cc_ok(c))
    return NULL;
  return jb_string_mutable_data(
      jh_list_at(&c->e.strings, jb_vector_size(&c->e.strings) - 1));
}

bool cc_fail(jh_compile_ctx_t *c, const jb_json_span_t *parts, size_t count) {
  if (cc_ok(c))
    (void)jh_fail(c->e.failure, HAL_ECONFIG, 2, parts, count);
  return false;
}

bool cc_unsupported(jh_compile_ctx_t *c, const jb_json_span_t *parts,
                    size_t count) {
  if (cc_ok(c))
    (void)jh_fail(c->e.failure, HAL_EUNSUPPORTED, 8, parts, count);
  return false;
}

bool cc_storage(jh_compile_ctx_t *c, hal_status_t status) {
  return esp_storage(&c->e, status);
}

jb_json_tree_view_t cc_root(const jb_json_tree_t *tree) {
  jb_json_tree_view_t root = {0};
  if (tree != NULL)
    (void)jb_json_tree_root(tree, &root);
  return root;
}

/* A tree of the JSON text a writer composed: {"v": value}, value kept. */
static jb_json_tree_t *from_writer(jh_compile_ctx_t *c, esp_json_t *json) {
  esp_json_end(json);
  jb_json_tree_t *outer = NULL, *tree = NULL;
  jb_json_tree_view_t value = {0};
  if (cc_storage(c, json->status) &&
      cc_storage(c,
                 jh_json_tree_from_text(jh_string_span(&json->text), &outer)) &&
      cc_storage(c, jb_json_tree_find(cc_root(outer), jh_span("v"), &value)))
    (void)cc_storage(c, jb_json_tree_copy(value, &tree_limits, &tree));
  jb_json_tree_destroy(outer);
  jb_string_destroy(&json->text);
  return tree;
}

static jb_json_tree_t *scalar(jh_compile_ctx_t *c, int kind, const char *text,
                              bool value) {
  esp_json_t json;
  if (!cc_ok(c) || !esp_json_begin(&c->e, &json))
    return NULL;
  if (kind == 0)
    esp_json_string(&json, "v", jh_span(text));
  else if (kind == 1) {
    esp_json_key(&json, "v");
    if (json.status == HAL_OK)
      json.status = jb_json_writer_bool(&json.writer, value);
  } else if (kind == 2) {
    esp_json_object(&json, "v");
    esp_json_object_end(&json);
  } else {
    esp_json_array(&json, "v");
    esp_json_array_end(&json);
  }
  return from_writer(c, &json);
}

jb_json_tree_t *cc_string(jh_compile_ctx_t *c, const char *text) {
  return scalar(c, 0, text, false);
}
jb_json_tree_t *cc_bool(jh_compile_ctx_t *c, bool value) {
  return scalar(c, 1, NULL, value);
}
jb_json_tree_t *cc_object(jh_compile_ctx_t *c) {
  return scalar(c, 2, NULL, false);
}
jb_json_tree_t *cc_array(jh_compile_ctx_t *c) {
  return scalar(c, 3, NULL, false);
}

jb_json_tree_t *cc_copy(jh_compile_ctx_t *c, jb_json_tree_view_t value) {
  jb_json_tree_t *tree = NULL;
  if (cc_ok(c))
    (void)cc_storage(c, jb_json_tree_copy(value, &tree_limits, &tree));
  return tree;
}

jb_json_tree_view_t cc_get(jb_json_tree_view_t object, const char *key) {
  return jh_json_field(object, key);
}

bool cc_has(jb_json_tree_view_t object, const char *key) {
  return object.tree != NULL && jh_json_type(object) == JB_JSON_OBJECT &&
         cc_get(object, key).tree != NULL;
}

bool cc_is(jb_json_tree_view_t value, jb_json_type_t type) {
  return value.tree != NULL && jh_json_type(value) == type;
}

bool cc_truthy(jb_json_tree_view_t value) {
  if (value.tree == NULL)
    return false;
  jb_json_span_t text = {0};
  switch (jh_json_type(value)) {
  case JB_JSON_NULL:
    return false;
  case JB_JSON_BOOL: {
    bool flag = false;
    (void)jb_json_tree_get_bool(value, &flag);
    return flag;
  }
  case JB_JSON_STRING:
  case JB_JSON_NUMBER: {
    (void)jb_json_tree_text(value, &text);
    if (jh_json_type(value) == JB_JSON_STRING)
      return text.length != 0;
    double number = 0;
    return jh_python_float(text, &number) != HAL_OK || number != 0;
  }
  default:
    return jh_json_count(value) != 0;
  }
}

const char *cc_str(jh_compile_ctx_t *c, jb_json_tree_view_t value) {
  jb_json_span_t text = {0};
  if (!cc_is(value, JB_JSON_STRING) || !jh_json_string(value, &text))
    return NULL;
  return CC_TEXT(c, text);
}

const char *cc_pystr(jh_compile_ctx_t *c, jb_json_tree_view_t value) {
  const char *text = cc_str(c, value);
  return text != NULL ? text : CC_TEXT(c, esp_repr_tree(&c->e, value));
}

void cc_set(jh_compile_ctx_t *c, jb_json_tree_t *tree, const char *key,
            jb_json_tree_view_t value) {
  if (cc_ok(c) && tree != NULL)
    (void)cc_storage(c, jb_json_tree_set(tree, jh_span(key), value));
}

void cc_set_text(jh_compile_ctx_t *c, jb_json_tree_t *tree, const char *key,
                 const char *text) {
  jb_json_tree_t *value = cc_string(c, text);
  cc_set(c, tree, key, cc_root(value));
  jb_json_tree_destroy(value);
}

void cc_erase(jh_compile_ctx_t *c, jb_json_tree_t *tree, const char *key) {
  if (!cc_ok(c) || tree == NULL)
    return;
  const hal_status_t status = jb_json_tree_erase(tree, jh_span(key));
  if (status != HAL_ENOENT)
    (void)cc_storage(c, status);
}

void cc_merge(jh_compile_ctx_t *c, jb_json_tree_t *tree,
              jb_json_tree_view_t overlay) {
  if (cc_ok(c) && tree != NULL && cc_is(overlay, JB_JSON_OBJECT))
    (void)cc_storage(c, jb_json_tree_merge(tree, overlay));
}

void cc_append(jh_compile_ctx_t *c, jb_json_tree_t *array,
               jb_json_tree_view_t value) {
  if (cc_ok(c) && array != NULL)
    (void)cc_storage(c, jb_json_tree_append(array, value));
}

void cc_append_text(jh_compile_ctx_t *c, jb_json_tree_t *array,
                    const char *text) {
  jb_json_tree_t *value = cc_string(c, text);
  cc_append(c, array, cc_root(value));
  jb_json_tree_destroy(value);
}

jb_json_tree_t *cc_section(jh_compile_ctx_t *c, jb_json_tree_view_t object,
                           const char *key) {
  const jb_json_tree_view_t value = cc_get(object, key);
  return cc_is(value, JB_JSON_OBJECT) ? cc_copy(c, value) : cc_object(c);
}

void cc_source(jh_compile_ctx_t *c, const char *key, const char *text) {
  cc_set_text(c, c->sources, key, text);
}

/* ---- Paths ---- */

#ifdef _WIN32
#define CC_SEPARATOR "\\"
#else
#define CC_SEPARATOR "/"
#endif

const char *cc_purepath(jh_compile_ctx_t *c, const char *text) {
  jb_string_t out = {0};
  if (!cc_ok(c) || !cc_storage(c, jb_string_init(&out, jb_heap_allocator())))
    return "";
  hal_status_t status = HAL_OK;
  const char *p = text;
#ifdef _WIN32
  if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
      p[1] == ':') {
    jh_put_span(&out, &status, (jb_json_span_t){p, 2});
    p += 2;
  }
  if (jb_string_size(&out) == 0 && jh_build_separator(p[0]) &&
      jh_build_separator(p[1]) && p[2] != '\0' && !jh_build_separator(p[2])) {
    /* A UNC share keeps its two leading separators. */
    jh_put(&out, &status, "\\\\");
    p += 2;
  } else if (jh_build_separator(*p)) {
    jh_put(&out, &status, "\\");
    while (jh_build_separator(*p))
      ++p;
  }
#else
  if (p[0] == '/') {
    /* PurePosixPath keeps exactly two leading slashes. */
    const bool two = p[1] == '/' && p[2] != '/';
    jh_put(&out, &status, two ? "//" : "/");
    while (*p == '/')
      ++p;
  }
#endif
  const size_t prefix = jb_string_size(&out);
  while (*p != '\0' && status == HAL_OK) {
    const char *end = p;
    while (*end != '\0' && !jh_build_separator(*end))
      ++end;
    const size_t length = (size_t)(end - p);
    if (length != 0 && !(length == 1 && p[0] == '.')) {
      if (jb_string_size(&out) > prefix)
        jh_put(&out, &status, CC_SEPARATOR);
      jh_put_span(&out, &status, (jb_json_span_t){p, length});
    }
    p = end;
    while (jh_build_separator(*p))
      ++p;
  }
  if (status == HAL_OK && jb_string_size(&out) == 0)
    jh_put(&out, &status, ".");
  const char *result =
      cc_storage(c, status) ? CC_TEXT(c, jh_string_span(&out)) : "";
  jb_string_destroy(&out);
  return result;
}

const char *cc_os_join(jh_compile_ctx_t *c, const char *base,
                       const char *name) {
  if (esp_absolute(name) || base[0] == '\0')
    return CC_TEXT(c, jh_span(name));
  const size_t length = strlen(base);
  const bool separated = jh_build_separator(base[length - 1]);
  return CC_TEXT(c, jh_span(base), jh_span(separated ? "" : CC_SEPARATOR),
                 jh_span(name));
}

const char *cc_path_join(jh_compile_ctx_t *c, const char *base,
                         const char *name) {
  return cc_purepath(c, cc_os_join(c, base, name));
}

bool cc_path_within(jh_compile_ctx_t *c, const char *child,
                    const char *parent) {
  /* Path.resolve() of a relative path starts at the working directory. */
  const char *a = esp_resolve(&c->e, child, c->base);
  const char *b = esp_resolve(&c->e, parent, c->base);
  return cc_ok(c) && jh_build_inside(a, b);
}

/* target_display_name: "<target>/<assembly or board>", or the target
 * ("default" without one). */
const char *cc_display_name(jh_compile_ctx_t *c, jb_json_tree_view_t config) {
  const jb_json_tree_view_t target = cc_get(config, "target");
  jb_json_tree_view_t board = cc_get(config, "assembly");
  if (!cc_truthy(board))
    board = cc_get(config, "board");
  const char *name = cc_truthy(target) ? cc_pystr(c, target) : "default";
  return cc_truthy(board) ? CC_TEXT(c, jh_span(name), jh_span("/"),
                                    jh_span(cc_pystr(c, board)))
                          : name;
}

/* Path(text).expanduser(), joined to the project when relative, as str(). */
const char *cc_project_absolute(jh_compile_ctx_t *c, const char *path) {
  const char *expanded = esp_expand_user(&c->e, cc_purepath(c, path));
  if (!esp_absolute(expanded))
    expanded = cc_os_join(c, c->project_dir, expanded);
  return cc_purepath(c, expanded);
}

const char *cc_json_dumps(jh_compile_ctx_t *c, jb_json_tree_view_t value) {
  jb_string_t out = {0};
  size_t written = 0;
  const jb_json_tree_format_t format = {SIZE_MAX / 2, 0, false, true};
  const char *result = "";
  if (cc_ok(c) && cc_storage(c, jb_string_init(&out, jb_heap_allocator())) &&
      cc_storage(c, jb_json_tree_format(value, &format, jh_string_sink, &out,
                                        &written)))
    result = CC_TEXT(c, jh_string_span(&out));
  jb_string_destroy(&out);
  return result;
}

/* ---- Files ---- */

/* load_json_file: comments, the last repeated name and NaN are accepted, a
 * missing file is an empty object and the root must be an object. */
static jb_json_tree_t *load_json(jh_compile_ctx_t *c, const char *path) {
  if (!cc_ok(c))
    return NULL;
  jh_json_location_t where;
  jh_json_location_init(&where);
  jh_failure_t error;
  jh_failure_init(&error);
  jb_json_tree_t *tree = NULL;
  bool exists = false;
  const hal_status_t status =
      jh_json_read_settings(&where, path, FILE_LIMIT, &tree, &exists, &error);
  if (status == HAL_ENOMEM)
    (void)cc_storage(c, status);
  else if (status == HAL_ETYPE)
    (void)CC_FAIL(c, jh_span(path), jh_span(" must contain a JSON object"));
  else if (status != HAL_OK)
    (void)CC_FAIL(c, jh_span("invalid JSON in "), jh_span(path), jh_span(": "),
                  jh_string_span(&error.message));
  jh_failure_destroy(&error);
  jh_json_location_destroy(&where);
  if (!cc_ok(c)) {
    jb_json_tree_destroy(tree);
    return NULL;
  }
  return tree;
}

/* ---- Registry: tooling_target_registry ---- */

static bool text_in(jb_json_tree_view_t array, const char *text) {
  for (size_t i = 0; i < jh_json_count(array); ++i) {
    jb_json_span_t item = {0};
    if (jh_json_string(jh_json_item(array, i), &item) &&
        jh_span_equal(item, jh_span(text)))
      return true;
  }
  return false;
}

/* _usb_identity: {"usbVid", "usbPid"} when both IDs are integers. */
static jb_json_tree_t *usb_identity(jh_compile_ctx_t *c,
                                    jb_json_tree_view_t usb, bool enabled) {
  int64_t vid = 0, pid = 0;
  if (!jh_json_int64(cc_get(usb, "vid"), &vid) ||
      !jh_json_int64(cc_get(usb, "pid"), &pid) ||
      !cc_is(cc_get(usb, "vid"), JB_JSON_NUMBER) ||
      !cc_is(cc_get(usb, "pid"), JB_JSON_NUMBER))
    return NULL;
  jb_json_tree_t *identity = cc_object(c);
  if (enabled) {
    jb_json_tree_t *flag = cc_bool(c, true);
    cc_set(c, identity, "enabled", cc_root(flag));
    jb_json_tree_destroy(flag);
  }
  cc_set(c, identity, "usbVid", cc_get(usb, "vid"));
  cc_set(c, identity, "usbPid", cc_get(usb, "pid"));
  return identity;
}

static void set_tree(jh_compile_ctx_t *c, jb_json_tree_t *tree, const char *key,
                     jb_json_tree_t *value) {
  cc_set(c, tree, key, cc_root(value));
  jb_json_tree_destroy(value);
}

static jb_json_tree_t *board_view(jh_compile_ctx_t *c,
                                  const jh_board_descriptor_t *board,
                                  const char *provider) {
  jb_json_tree_t *view = cc_object(c);
  cc_set_text(c, view, "id", jb_string_data(&board->id));
  jb_json_tree_t *components = cc_array(c);
  const jb_json_tree_view_t declared = cc_get(board->root, "components");
  jb_vector_t names = {0};
  if (esp_list_init(&c->e, &names)) {
    for (size_t i = 0; i < jh_json_count(declared); ++i)
      (void)esp_push(&c->e, &names, jh_json_key(jh_json_item(declared, i)));
    jh_list_sort(&names);
    for (size_t i = 0; i < jb_vector_size(&names); ++i)
      cc_append_text(c, components, esp_at(&names, i));
  }
  jh_list_destroy(&names);
  set_tree(c, view, "components", components);
  jb_json_tree_t *identity =
      usb_identity(c, cc_get(cc_get(board->root, "programming"), "usb"), true);
  if (identity != NULL)
    set_tree(c, view, "identity", identity);
  if (strcmp(provider, "pico-sdk") == 0 ||
      strcmp(provider, "jh-stm32-baremetal") == 0) {
    jb_json_tree_t *cache = cc_object(c);
    cc_set_text(c, cache, "JH_BOARD", jb_string_data(&board->id));
    set_tree(c, view, "cache", cache);
  }
  return view;
}

static jb_json_tree_t *target_view(jh_compile_ctx_t *c,
                                   const jh_board_descriptor_t *target,
                                   const char *provider) {
  const char *id = jb_string_data(&target->id);
  jb_json_tree_t *view = cc_object(c);
  jb_json_tree_t *cache = cc_object(c), *upload = cc_object(c);
  const char *toolchain = "cmake";
  cc_set_text(c, view, "id", id);
  cc_set_text(c, view, "provider", provider);
  if (strcmp(provider, "pico-sdk") == 0) {
    cc_set_text(c, cache, "JH_TARGET", id);
    cc_set_text(c, cache, "PICO_SDK_PATH", "${jhRoot}/third_party/pico-sdk");
    if (jh_span_equal(
            jh_build_descriptor_text(target->root, "architecture", "isa"),
            jh_span("riscv32")))
      cc_set_text(c, cache, "PICO_TOOLCHAIN_PATH",
                  "${jhRoot}/third_party/riscv-toolchain");
    cc_set_text(c, upload, "strategy", "uf2");
  } else if (strcmp(provider, "jh-stm32-baremetal") == 0) {
    cc_set_text(c, cache, "JH_TARGET", id);
    cc_set_text(c, cache, "CMAKE_TOOLCHAIN_FILE",
                "${jhRoot}/link_libraries/stm32_lib/toolchain_stm32g474.cmake");
    jb_json_tree_t *openocd = cc_object(c);
    cc_set_text(c, upload, "strategy", "openocd");
    cc_set_text(c, openocd, "interface", "interface/stlink.cfg");
    cc_set_text(c, openocd, "target", "target/stm32g4x.cfg");
    set_tree(c, upload, "openocd", openocd);
  } else {
    toolchain = "esp-idf";
    cc_set_text(c, upload, "strategy", "esp-idf");
    jb_json_tree_t *esp = cc_object(c);
    cc_set_text(c, esp, "builder", "jh");
    cc_set_text(c, esp, "runner", "${jhRoot}/scripts/build_esp_idf.py");
    cc_set_text(c, esp, "artifactManifest",
                "${buildDir}/jh_esp_idf_artifacts.json");
    set_tree(c, view, "espIdf", esp);
  }
  cc_set_text(c, view, "toolchain", toolchain);
  cc_set(c, view, "defaultBoard", cc_get(target->root, "defaultBoard"));
  cc_set(c, view, "status", cc_get(target->root, "status"));
  const jb_json_tree_view_t required = cc_get(target->root, "requiredFeatures");
  if (required.tree != NULL)
    cc_set(c, view, "requiredFeatures", required);
  set_tree(c, view, "cache", cache);
  set_tree(c, view, "upload", upload);
  jb_json_tree_t *identity =
      usb_identity(c, cc_get(target->root, "usbDevice"), false);
  if (identity != NULL)
    set_tree(c, view, "identity", identity);
  jb_json_tree_t *boards = cc_array(c);
  for (size_t i = 0; i < jb_vector_size(&c->e.boards.boards) && cc_ok(c); ++i) {
    const jh_board_descriptor_t *board =
        jb_vector_const_at(&c->e.boards.boards, i);
    if (!text_in(cc_get(board->root, "compatibleTargets"), id))
      continue;
    jb_json_tree_t *item = board_view(c, board, provider);
    cc_append(c, boards, cc_root(item));
    jb_json_tree_destroy(item);
  }
  set_tree(c, view, "boards", boards);
  return view;
}

static bool load_registries(jh_compile_ctx_t *c) {
  if (c->registry != NULL)
    return cc_ok(c);
  const char *config_root = esp_join(&c->e, c->root, "config");
  const char *boards_root = esp_join(&c->e, c->root, "boards");
  if (!cc_ok(c) ||
      jh_feature_registry_load(&c->e.features, config_root, FILE_LIMIT,
                               c->e.failure) != HAL_OK ||
      jh_board_registry_load_with_features(&c->e.boards, c->root, boards_root,
                                           FILE_LIMIT, &c->e.features,
                                           c->e.failure) != HAL_OK)
    return false;
  c->features_loaded = true;
  c->registry = cc_object(c);
  for (size_t i = 0; i < jb_vector_size(&c->e.boards.targets) && cc_ok(c);
       ++i) {
    const jh_board_descriptor_t *target =
        jb_vector_const_at(&c->e.boards.targets, i);
    const char *provider =
        CC_TEXT(c, jh_build_descriptor_text(target->root, "build", "provider"));
    if (strcmp(provider, "pico-sdk") != 0 &&
        strcmp(provider, "jh-stm32-baremetal") != 0 &&
        strcmp(provider, "esp-idf") != 0)
      continue;
    set_tree(c, c->registry, jb_string_data(&target->id),
             target_view(c, target, provider));
  }
  return cc_ok(c);
}

static jb_json_tree_view_t registry_target(jh_compile_ctx_t *c,
                                           const char *target) {
  return target != NULL ? cc_get(cc_root(c->registry), target)
                        : (jb_json_tree_view_t){0};
}

static bool registry_has_board(jb_json_tree_view_t target, const char *board) {
  const jb_json_tree_view_t boards = cc_get(target, "boards");
  for (size_t i = 0; i < jh_json_count(boards); ++i)
    if (jh_span_equal(
            jh_build_descriptor_text(jh_json_item(boards, i), "id", NULL),
            jh_span(board)))
      return true;
  return false;
}

static jb_json_tree_view_t registry_board(jh_compile_ctx_t *c,
                                          jb_json_tree_view_t target,
                                          const char *board) {
  const char *selected =
      board != NULL ? board : cc_str(c, cc_get(target, "defaultBoard"));
  const jb_json_tree_view_t boards = cc_get(target, "boards");
  for (size_t i = 0; selected != NULL && i < jh_json_count(boards); ++i)
    if (jh_span_equal(
            jh_build_descriptor_text(jh_json_item(boards, i), "id", NULL),
            jh_span(selected)))
      return jh_json_item(boards, i);
  return (jb_json_tree_view_t){0};
}

/* ---- Board selection ---- */

typedef struct {
  jb_json_tree_view_t board; /* No tree: None. */
  const char *source;
  bool scoped;
} candidate_t;

/* select_registry_board; *board receives NULL for None. */
static bool select_board(jh_compile_ctx_t *c, const char *target,
                         const candidate_t *candidates, size_t count,
                         const char **board, const char **source) {
  *board = NULL;
  *source = "default";
  const jb_json_tree_view_t desc = registry_target(c, target);
  if (!cc_is(desc, JB_JSON_OBJECT)) {
    for (size_t i = 0; i < count; ++i)
      if (candidates[i].board.tree != NULL &&
          !cc_is(candidates[i].board, JB_JSON_NULL)) {
        *board = cc_pystr(c, candidates[i].board);
        *source = candidates[i].source;
        return cc_ok(c);
      }
    return cc_ok(c);
  }
  const jb_json_tree_view_t boards = cc_get(desc, "boards");
  for (size_t i = 0; i < count && cc_ok(c); ++i) {
    if (candidates[i].board.tree == NULL ||
        cc_is(candidates[i].board, JB_JSON_NULL))
      continue;
    const char *name = cc_pystr(c, candidates[i].board);
    if (jh_json_count(boards) == 0 || registry_has_board(desc, name)) {
      *board = name;
      *source = candidates[i].source;
      return cc_ok(c);
    }
    jb_string_t known = {0}, owners = {0};
    hal_status_t status = jb_string_init(&known, jb_heap_allocator());
    if (status == HAL_OK)
      status = jb_string_init(&owners, jb_heap_allocator());
    jh_put(&known, &status, target);
    jh_put(&known, &status, " boards: ");
    for (size_t b = 0; b < jh_json_count(boards); ++b) {
      if (b != 0)
        jh_put(&known, &status, ", ");
      jh_put_span(
          &known, &status,
          jh_build_descriptor_text(jh_json_item(boards, b), "id", NULL));
    }
    /* The registry object keeps the targets in name order. */
    const jb_json_tree_view_t all = cc_root(c->registry);
    for (size_t t = 0; t < jh_json_count(all); ++t)
      if (registry_has_board(jh_json_item(all, t), name)) {
        if (jb_string_size(&owners) != 0)
          jh_put(&owners, &status, ", ");
        jh_put_span(&owners, &status, jh_json_key(jh_json_item(all, t)));
      }
    if (cc_storage(c, status)) {
      if (jb_string_size(&owners) == 0)
        (void)CC_FAIL(c, jh_span("[JH-CFG-BOARD] "),
                      jh_span(candidates[i].source), jh_span(": board '"),
                      jh_span(name),
                      jh_span("' is not in the board registry; "),
                      jh_string_span(&known));
      else if (candidates[i].scoped)
        (void)CC_FAIL(c, jh_span("[JH-CFG-BOARD] "),
                      jh_span(candidates[i].source), jh_span(": board '"),
                      jh_span(name), jh_span("' belongs to "),
                      jh_string_span(&owners), jh_span(", not to target "),
                      jh_span(target), jh_span("; "), jh_string_span(&known));
    }
    jb_string_destroy(&owners);
    jb_string_destroy(&known);
  }
  if (!cc_ok(c))
    return false;
  *board = cc_str(c, cc_get(desc, "defaultBoard"));
  *source = CC_TEXT(c, jh_span("registry:"), jh_span(target),
                    jh_span(".defaultBoard"));
  return cc_ok(c);
}

static const char *manifest_target_text(jh_compile_ctx_t *c,
                                        jb_json_tree_view_t manifest) {
  const jb_json_tree_view_t target = cc_get(manifest, "target");
  return cc_truthy(target) ? cc_pystr(c, target) : "rp2040";
}

/* manifest_board_candidates(manifest, target). */
static void manifest_candidates(jh_compile_ctx_t *c,
                                jb_json_tree_view_t manifest,
                                const char *target, candidate_t out[2]) {
  const jb_json_tree_view_t overlay =
      cc_get(cc_get(manifest, "targetProfiles"), target);
  out[0].board = cc_is(overlay, JB_JSON_OBJECT) ? cc_get(overlay, "board")
                                                : (jb_json_tree_view_t){0};
  out[0].source =
      CC_TEXT(c, jh_span(MANIFEST_LABEL " targetProfiles."), jh_span(target));
  out[0].scoped = true;
  out[1].board = cc_get(manifest, "board");
  out[1].source = MANIFEST_LABEL;
  out[1].scoped = strcmp(target, manifest_target_text(c, manifest)) == 0;
}

static bool validate_manifest_boards(jh_compile_ctx_t *c,
                                     jb_json_tree_view_t manifest) {
  const char *board = NULL, *source = NULL;
  const char *target = manifest_target_text(c, manifest);
  candidate_t candidates[2];
  manifest_candidates(c, manifest, target, candidates);
  if (!select_board(c, target, candidates + 1, 1, &board, &source))
    return false;
  const jb_json_tree_view_t profiles = cc_get(manifest, "targetProfiles");
  if (!cc_is(profiles, JB_JSON_OBJECT))
    return cc_ok(c);
  for (size_t i = 0; i < jh_json_count(profiles) && cc_ok(c); ++i) {
    const char *name = CC_TEXT(c, jh_json_key(jh_json_item(profiles, i)));
    manifest_candidates(c, manifest, name, candidates);
    (void)select_board(c, name, candidates, 1, &board, &source);
  }
  return cc_ok(c);
}

/* ---- Tokens ---- */

static const char *replace_all(jh_compile_ctx_t *c, const char *text,
                               const char *token, const char *value) {
  if (strstr(text, token) == NULL)
    return text;
  jb_string_t out = {0};
  hal_status_t status = jb_string_init(&out, jb_heap_allocator());
  const size_t length = strlen(token);
  const char *match = NULL;
  while (status == HAL_OK && (match = strstr(text, token)) != NULL) {
    jh_put_span(&out, &status, (jb_json_span_t){text, (size_t)(match - text)});
    jh_put(&out, &status, value);
    text = match + length;
  }
  jh_put(&out, &status, text);
  const char *result =
      cc_storage(c, status) ? CC_TEXT(c, jh_string_span(&out)) : "";
  jb_string_destroy(&out);
  return result;
}

static const char *name_of(const char *path) {
  const char *name = path;
  for (const char *p = path; *p; ++p)
    if (jh_build_separator(*p))
      name = p + 1;
  return name;
}

/* expand_project_vars of one string with the config as it is now. */
static const char *expand(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                          const char *text, const char *target) {
  const jb_json_tree_view_t build = cc_get(config, "buildDir");
  const char *build_dir = build.tree != NULL
                              ? cc_pystr(c, build)
                              : cc_path_join(c, c->project_dir, ".build");
  const jb_json_tree_view_t module = cc_get(config, "module");
  const jb_json_tree_view_t project = cc_get(config, "project");
  const char *parent = esp_parent(&c->e, c->project_dir);
  const char *const tokens[] = {
      "${jhRoot}", "${project}",     "${projectDir}", "${workspaceFolder}",
      "${module}", "${projectName}", "${buildDir}",   "${target}"};
  const char *const values[] = {
      c->root,
      c->project_dir,
      c->project_dir,
      c->project_dir,
      module.tree != NULL ? cc_pystr(c, module) : name_of(c->project_dir),
      project.tree != NULL ? cc_pystr(c, project) : name_of(parent),
      build_dir,
      target};
  for (size_t i = 0; i < sizeof tokens / sizeof *tokens && cc_ok(c); ++i)
    if (values[i] != NULL)
      text = replace_all(c, text, tokens[i], values[i]);
  return text;
}

static void expand_member(jh_compile_ctx_t *c, jb_json_tree_t *object,
                          jb_json_tree_view_t config, const char *key,
                          const char *target) {
  const char *text = cc_str(c, cc_get(cc_root(object), key));
  if (text != NULL)
    cc_set_text(c, object, key, expand(c, config, text, target));
}

static void expand_values(jh_compile_ctx_t *c, jb_json_tree_t *object,
                          jb_json_tree_view_t config, const char *target) {
  for (size_t i = 0; i < jh_json_count(cc_root(object)) && cc_ok(c); ++i) {
    const char *key = CC_TEXT(c, jh_json_key(jh_json_item(cc_root(object), i)));
    expand_member(c, object, config, key, target);
  }
}

/* expand_config_sections, in place. */
static void expand_sections(jh_compile_ctx_t *c, jb_json_tree_t *config,
                            const char *target) {
  expand_member(c, config, cc_root(config), "buildDir", target);
  expand_member(c, config, cc_root(config), "cmakeBuildDir", target);
  static const char *const sections[] = {"artifacts", "upload", "ota", "hooks",
                                         "espIdf"};
  for (size_t i = 0; i < sizeof sections / sizeof *sections && cc_ok(c); ++i) {
    const jb_json_tree_view_t section = cc_get(cc_root(config), sections[i]);
    if (!cc_is(section, JB_JSON_OBJECT))
      continue;
    jb_json_tree_t *copy = cc_copy(c, section);
    expand_values(c, copy, cc_root(config), target);
    set_tree(c, config, sections[i], copy);
  }
  const jb_json_tree_view_t cmake = cc_get(cc_root(config), "cmake");
  if (!cc_is(cmake, JB_JSON_OBJECT))
    return;
  jb_json_tree_t *copy = cc_copy(c, cmake);
  expand_member(c, copy, cc_root(config), "sourceDir", target);
  const jb_json_tree_view_t cache = cc_get(cc_root(copy), "cache");
  if (cc_is(cache, JB_JSON_OBJECT)) {
    jb_json_tree_t *values = cc_copy(c, cache);
    expand_values(c, values, cc_root(config), target);
    set_tree(c, copy, "cache", values);
  }
  set_tree(c, config, "cmake", copy);
}

static bool contains_target_token(jb_json_tree_view_t value) {
  jb_json_span_t text = {0};
  if (cc_is(value, JB_JSON_STRING) && jh_json_string(value, &text) &&
      text.length >= 9)
    for (size_t i = 0; i + 9 <= text.length; ++i)
      if (memcmp(text.data + i, "${target}", 9) == 0)
        return true;
  const bool object = cc_is(value, JB_JSON_OBJECT);
  if (!object && !cc_is(value, JB_JSON_ARRAY))
    return false;
  for (size_t i = 0; i < jh_json_count(value); ++i) {
    if (object) {
      const jb_json_span_t key = jh_json_key(jh_json_item(value, i));
      for (size_t k = 0; k + 9 <= key.length; ++k)
        if (memcmp(key.data + k, "${target}", 9) == 0)
          return true;
    }
    if (contains_target_token(jh_json_item(value, i)))
      return true;
  }
  return false;
}

/* ---- Identity ---- */

/* SerialIdentityExpectation.from_config: the USB IDs, validated. */
static bool identity_ids(jh_compile_ctx_t *c, jb_json_tree_view_t identity,
                         const char *prefix, int32_t *vid, int32_t *pid) {
  static const char *const keys[] = {"usbVid", "usbPid"};
  int32_t *outs[] = {vid, pid};
  for (size_t i = 0; i < 2 && cc_ok(c); ++i) {
    jh_usb_id_kind_t kind = JH_USB_ID_ABSENT;
    uint32_t value = 0;
    *outs[i] = -1;
    if (!cc_storage(
            c, jh_usb_identifier(cc_get(identity, keys[i]), &kind, &value)))
      return false;
    if (kind == JH_USB_ID_VALUE)
      *outs[i] = (int32_t)value;
    else if (kind == JH_USB_ID_RANGE)
      return CC_FAIL(c, jh_span(prefix), jh_span(keys[i]),
                     jh_span(" must be in range 0..65535"));
    else if (kind == JH_USB_ID_INVALID)
      return CC_FAIL(c, jh_span(prefix), jh_span(keys[i]),
                     jh_span(" must be an integer or a USB hexadecimal "
                             "identifier"));
  }
  return cc_ok(c);
}

/* ---- resolve_target_profile ---- */

static void pin_target_board(jh_compile_ctx_t *c, jb_json_tree_t *config) {
  const jb_json_tree_view_t root = cc_root(config);
  const char *toolchain = cc_str(c, cc_get(root, "toolchain"));
  if (toolchain == NULL || strcmp(toolchain, "cmake") != 0 ||
      !cc_is(cc_get(root, "cmake"), JB_JSON_OBJECT))
    return;
  jb_json_tree_t *cmake = cc_copy(c, cc_get(root, "cmake"));
  jb_json_tree_t *cache = cc_section(c, cc_root(cmake), "cache");
  const bool assembly = cc_truthy(cc_get(root, "assembly"));
  if (assembly)
    cc_erase(c, cache, "JH_BOARD");
  const char *const keys[] = {"JH_TARGET",
                              assembly ? "JH_ASSEMBLY" : "JH_BOARD"};
  const char *const fields[] = {"target", assembly ? "assembly" : "board"};
  for (size_t i = 0; i < 2; ++i)
    if (cc_truthy(cc_get(cc_root(config), fields[i])))
      cc_set(c, cache, keys[i], cc_get(cc_root(config), fields[i]));
  set_tree(c, cmake, "cache", cache);
  set_tree(c, config, "cmake", cmake);
}

static bool set_upload_strategy(jh_compile_ctx_t *c, jb_json_tree_t *config,
                                jb_json_tree_view_t strategy,
                                const char *target) {
  jb_json_tree_t *upload = cc_section(c, cc_root(config), "upload");
  cc_set(c, upload, "strategy", strategy);
  set_tree(c, config, "upload", upload);
  cc_source(c, "upload",
            CC_TEXT(c, jh_span("registry:"), jh_span(target),
                    jh_span(".upload.strategy")));
  return cc_ok(c);
}

typedef struct {
  const char *target, *board, *local_board;
  const char *override_source;
} profile_request_t;

static bool resolve_target_profile(jh_compile_ctx_t *c, jb_json_tree_t **config,
                                   const profile_request_t *request) {
  jb_json_tree_view_t root = cc_root(*config);
  const jb_json_tree_view_t profiles = cc_get(root, "targetProfiles");
  const jb_json_tree_view_t manifest_target = cc_get(root, "target");
  const jb_json_tree_view_t manifest_board = cc_get(root, "board");
  const bool target_set =
      manifest_target.tree != NULL && !cc_is(manifest_target, JB_JSON_NULL);
  const bool has_any =
      request->target != NULL || request->board != NULL ||
      request->local_board != NULL || target_set ||
      (manifest_board.tree != NULL && !cc_is(manifest_board, JB_JSON_NULL)) ||
      cc_is(profiles, JB_JSON_OBJECT) || contains_target_token(root);
  if (!has_any)
    return cc_ok(c);
  if (!load_registries(c))
    return false;
  const char *active =
      request->target != NULL ? request->target : manifest_target_text(c, root);
  const jb_json_tree_view_t desc = registry_target(c, active);
  if (!validate_manifest_boards(c, root))
    return false;
  root = cc_root(*config);
  const jb_json_tree_view_t overlay =
      cc_get(cc_get(root, "targetProfiles"), active);
  const bool has_overlay = cc_is(overlay, JB_JSON_OBJECT);
  candidate_t candidates[4];
  jb_json_tree_t *cli_board =
      request->board != NULL ? cc_string(c, request->board) : NULL;
  jb_json_tree_t *local_board =
      request->local_board != NULL ? cc_string(c, request->local_board) : NULL;
  candidates[0] = (candidate_t){cc_root(cli_board), "cli", true};
  candidates[1] = (candidate_t){cc_root(local_board), LOCAL_LABEL, false};
  manifest_candidates(c, root, active, candidates + 2);
  const char *board = NULL, *board_source = NULL;
  (void)select_board(c, active, candidates, 4, &board, &board_source);
  jb_json_tree_destroy(cli_board);
  jb_json_tree_destroy(local_board);
  if (!cc_ok(c))
    return false;

  /* Effective layers: registry < base manifest < targetProfiles.<active>. */
  jb_json_tree_t *layer = cc_object(c);
  jb_json_tree_t *board_identity = NULL;
  if (cc_is(desc, JB_JSON_OBJECT)) {
    if (cc_is(cc_get(desc, "identity"), JB_JSON_OBJECT))
      cc_set(c, layer, "identity", cc_get(desc, "identity"));
    if (cc_truthy(cc_get(desc, "toolchain")))
      cc_set(c, layer, "toolchain", cc_get(desc, "toolchain"));
    jb_json_tree_t *cache = cc_section(c, desc, "cache");
    const jb_json_tree_view_t board_desc = registry_board(c, desc, board);
    cc_merge(c, cache, cc_get(board_desc, "cache"));
    for (size_t i = 0; i < jh_json_count(cc_root(cache)) && cc_ok(c); ++i) {
      const jb_json_tree_view_t item = jh_json_item(cc_root(cache), i);
      const char *text = cc_str(c, item);
      if (text != NULL)
        cc_set_text(c, cache, CC_TEXT(c, jh_json_key(item)),
                    replace_all(c, text, "${jhRoot}", c->root));
    }
    if (jh_json_count(cc_root(cache)) != 0) {
      jb_json_tree_t *cmake = cc_object(c);
      set_tree(c, cmake, "cache", cache);
      set_tree(c, layer, "cmake", cmake);
    } else {
      jb_json_tree_destroy(cache);
    }
    if (cc_is(cc_get(desc, "upload"), JB_JSON_OBJECT))
      cc_set(c, layer, "upload", cc_get(desc, "upload"));
    if (cc_is(cc_get(desc, "espIdf"), JB_JSON_OBJECT))
      cc_set(c, layer, "espIdf", cc_get(desc, "espIdf"));
    if (cc_is(cc_get(board_desc, "identity"), JB_JSON_OBJECT)) {
      board_identity = cc_copy(c, cc_get(board_desc, "identity"));
      jb_json_tree_t *identity = cc_section(c, cc_root(layer), "identity");
      cc_merge(c, identity, cc_root(board_identity));
      set_tree(c, layer, "identity", identity);
    }
  }
  jb_json_tree_t *merged = cc_copy(c, cc_root(layer));
  jb_json_tree_t *base = cc_copy(c, root);
  cc_erase(c, base, "targetProfiles");
  cc_merge(c, merged, cc_root(base));
  if (has_overlay)
    cc_merge(c, merged, overlay);
  jb_json_tree_destroy(base);
  if (cc_truthy(cc_get(cc_root(layer), "toolchain"))) {
    cc_set(c, merged, "toolchain", cc_get(cc_root(layer), "toolchain"));
    cc_source(c, "toolchain",
              CC_TEXT(c, jh_span("registry:"), jh_span(active),
                      jh_span(".build.provider")));
  }
  if (cc_is(cc_get(cc_root(layer), "espIdf"), JB_JSON_OBJECT)) {
    cc_set(c, merged, "espIdf", cc_get(cc_root(layer), "espIdf"));
    cc_source(c, "espIdf",
              CC_TEXT(c, jh_span("registry:"), jh_span(active),
                      jh_span(".build.provider")));
  }
  if (board_identity != NULL) {
    jb_json_tree_t *effective = cc_object(c);
    const jb_json_tree_view_t project = cc_get(cc_root(merged), "identity");
    static const char *const kept[] = {"usbSerialNumber", "usbInterface",
                                       "usbLocation"};
    if (cc_truthy(project))
      for (size_t i = 0; i < 3; ++i)
        if (cc_has(project, kept[i]))
          cc_set(c, effective, kept[i], cc_get(project, kept[i]));
    cc_merge(c, effective, cc_root(board_identity));
    int32_t vid = 0, pid = 0;
    (void)identity_ids(c, cc_root(effective), "", &vid, &pid);
    set_tree(c, merged, "identity", effective);
    cc_source(c, "identity",
              CC_TEXT(c, jh_span("registry:"), jh_span(active),
                      jh_span(".boards."),
                      jh_span(board != NULL ? board : "None"),
                      jh_span(".programming.usb")));
  }
  jb_json_tree_destroy(board_identity);
  /* The overlay is read below; keep its keys before the config is replaced. */
  jb_json_tree_t *overlay_copy = has_overlay ? cc_copy(c, overlay) : NULL;
  const bool manifest_target_set = target_set;
  const char *manifest_active = manifest_target_text(c, root);
  jb_json_tree_destroy(*config);
  *config = merged;
  expand_sections(c, *config, active);
  root = cc_root(*config);

  const bool switched = strcmp(active, manifest_active) != 0;
  const jb_json_tree_view_t registry_upload = cc_get(desc, "upload");
  const jb_json_tree_view_t strategy = cc_get(registry_upload, "strategy");
  const char *toolchain = cc_str(c, cc_get(root, "toolchain"));
  const bool esp = toolchain != NULL && strcmp(toolchain, "esp-idf") == 0;
  if (esp && cc_truthy(strategy))
    (void)set_upload_strategy(c, *config, strategy, active);
  const jb_json_tree_view_t overlay_upload =
      cc_get(cc_root(overlay_copy), "upload");
  if (switched && !esp && cc_truthy(strategy) &&
      !(cc_is(overlay_upload, JB_JSON_OBJECT) &&
        cc_truthy(cc_get(overlay_upload, "strategy"))))
    (void)set_upload_strategy(c, *config, strategy, active);

  cc_set_text(c, *config, "target", active);
  cc_source(c, "target",
            request->target != NULL ? request->override_source
            : manifest_target_set   ? MANIFEST_LABEL
                                    : "default");
  const char *overlay_source =
      CC_TEXT(c, jh_span("registry/targetProfiles:"), jh_span(active));
  jb_json_tree_view_t keys[2] = {cc_root(layer), cc_root(overlay_copy)};
  for (size_t k = 0; k < 2 && cc_ok(c); ++k)
    for (size_t i = 0;
         keys[k].tree != NULL && i < jh_json_count(keys[k]) && cc_ok(c); ++i) {
      const char *key = CC_TEXT(c, jh_json_key(jh_json_item(keys[k], i)));
      if (strcmp(key, "targetProfiles") != 0 && strcmp(key, "target") != 0 &&
          strcmp(key, "board") != 0 && !cc_has(cc_root(c->sources), key))
        cc_source(c, key, overlay_source);
    }
  jb_json_tree_destroy(overlay_copy);
  jb_json_tree_destroy(layer);
  if (board != NULL) {
    cc_set_text(c, *config, "board", board);
    cc_source(c, "board", board_source);
  }
  pin_target_board(c, *config);
  return cc_ok(c);
}

/* ---- Device tree ---- */

typedef struct {
  const char *name, *target, *board;
  const char *origin; /* explicit, automatic or the stored key's source. */
} assembly_t;

static bool hardware_registry(jh_compile_ctx_t *c) {
  if (c->e.hardware_loaded)
    return cc_ok(c);
  if (jh_hardware_registry_load(&c->e.hardware, c->root, FILE_LIMIT,
                                c->e.failure) != HAL_OK)
    return false;
  c->e.hardware_loaded = true;
  return true;
}

/* A hardware failure of jh, as the message the Python adapter raises. */
static bool hardware_failure(jh_compile_ctx_t *c) {
  if (c->e.failure->status != HAL_OK && c->e.failure->status != HAL_ENOMEM)
    c->e.failure->exit_code = 2;
  return false;
}

static void device_tree_findings(jh_compile_ctx_t *c,
                                 jb_json_tree_view_t config,
                                 jb_json_tree_view_t local, jb_string_t *out) {
  static const char hint[] =
      "device_tree.json selects target and board through its assemblies";
  hal_status_t status = HAL_OK;
  const char *board = c->options->board;
#define LINE(...)                                                              \
  do {                                                                         \
    if (jb_string_size(out) != 0)                                              \
      jh_put(out, &status, "\n");                                              \
    const jb_json_span_t parts_[] = {__VA_ARGS__};                             \
    for (size_t p_ = 0; p_ < sizeof parts_ / sizeof *parts_; ++p_)             \
      jh_put_span(out, &status, parts_[p_]);                                   \
  } while (0)
  if (board[0] != '\0')
    LINE(jh_span("[JH-HW-BOARD] --board "), jh_span(board), jh_span(": "),
         jh_span(hint), jh_span("; use --assembly"));
  static const char *const selectors[] = {"target", "board"};
  for (size_t i = 0; i < 2; ++i)
    if (cc_has(config, selectors[i]) &&
        !cc_is(cc_get(config, selectors[i]), JB_JSON_NULL))
      LINE(jh_span("[JH-HW-BOARD] " MANIFEST_LABEL ": '"),
           jh_span(selectors[i]), jh_span("': "), jh_span(hint),
           jh_span("; store the assembly key as 'assembly'"));
  const jb_json_tree_view_t profiles = cc_get(config, "targetProfiles");
  const bool has_profiles = cc_is(profiles, JB_JSON_OBJECT);
  for (size_t i = 0; has_profiles && i < jh_json_count(profiles); ++i) {
    const jb_json_tree_view_t profile = jh_json_item(profiles, i);
    if (cc_is(profile, JB_JSON_OBJECT) && cc_has(profile, "board"))
      LINE(jh_span("[JH-HW-BOARD] " MANIFEST_LABEL ": targetProfiles."),
           jh_json_key(profile), jh_span(".board: "), jh_span(hint));
  }
  for (size_t i = 0; i <= (has_profiles ? jh_json_count(profiles) : 0); ++i) {
    jb_json_tree_view_t cmake = cc_get(config, "cmake");
    const char *location = "cmake.cache";
    if (i != 0) {
      const jb_json_tree_view_t profile = jh_json_item(profiles, i - 1);
      if (!cc_is(profile, JB_JSON_OBJECT))
        continue;
      cmake = cc_get(profile, "cmake");
      location = CC_TEXT(c, jh_span("targetProfiles."), jh_json_key(profile),
                         jh_span(".cmake.cache"));
    }
    const jb_json_tree_view_t cache = cc_get(cmake, "cache");
    static const char *const keys[] = {"JH_BOARD", "JH_ASSEMBLY"};
    for (size_t k = 0; k < 2; ++k)
      if (cc_is(cache, JB_JSON_OBJECT) && cc_has(cache, keys[k]))
        LINE(jh_span("[JH-HW-BOARD] " MANIFEST_LABEL ": "), jh_span(location),
             jh_span("."), jh_span(keys[k]), jh_span(": "), jh_span(hint),
             jh_span("; store the assembly key as 'assembly'"));
  }
  for (size_t i = 0; i < 2; ++i)
    if (cc_has(local, selectors[i]) &&
        !cc_is(cc_get(local, selectors[i]), JB_JSON_NULL))
      LINE(jh_span("[JH-HW-BOARD] " LOCAL_LABEL ": '"), jh_span(selectors[i]),
           jh_span("' was selected for the board path; select an assembly: "
                   "jh-vscode select-board --project <path> --assembly "
                   "<key>"));
#undef LINE
  (void)cc_storage(c, status);
}

static bool assembly_entry(jh_compile_ctx_t *c, jb_json_tree_view_t item,
                           assembly_t *out) {
  out->name = cc_str(c, cc_get(item, "name"));
  out->target = cc_str(c, cc_get(item, "target"));
  out->board = cc_str(c, cc_get(item, "board"));
  return cc_ok(c) && out->name != NULL && out->target != NULL &&
         out->board != NULL;
}

/* choose_assembly and device_tree_project.choose. */
static bool choose_assembly(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                            jb_json_tree_view_t local, const char *tree_path,
                            assembly_t *chosen) {
  const jh_compile_options_t *o = c->options;
  const bool explicit_choice = o->assembly[0] != '\0' || o->target[0] != '\0';
  jb_string_t findings = {0};
  if (!cc_storage(c, jb_string_init(&findings, jb_heap_allocator())))
    return false;
  device_tree_findings(
      c, config, explicit_choice ? (jb_json_tree_view_t){0} : local, &findings);
  if (cc_ok(c) && jb_string_size(&findings) != 0)
    (void)CC_FAIL(c, jh_string_span(&findings));
  jb_string_destroy(&findings);
  if (!cc_ok(c) || !hardware_registry(c))
    return false;
  jh_hardware_t hardware = {0};
  jb_string_t text = {0};
  jb_json_tree_t *list = NULL;
  bool done = false;
  if (!cc_storage(c, jb_string_init(&text, jb_heap_allocator())) ||
      jh_hardware_load(&hardware, &c->e.hardware, tree_path, c->e.failure) !=
          HAL_OK ||
      jh_hardware_list(&hardware, &text, c->e.failure) != HAL_OK) {
    (void)hardware_failure(c);
    goto out;
  }
  if (o->assembly[0] == '\0' &&
      cc_storage(c, jh_json_tree_from_text(jh_string_span(&text), &list))) {
    const jb_json_tree_view_t stored[] = {cc_get(local, "assembly"),
                                          cc_get(config, "assembly")};
    const char *labels[] = {LOCAL_LABEL, MANIFEST_LABEL};
    for (size_t s = 0; s < 2 && !done && cc_ok(c); ++s) {
      if (!cc_truthy(stored[s]))
        continue;
      const char *key = cc_pystr(c, stored[s]);
      bool found = false;
      jb_vector_t known = {0};
      if (!esp_list_init(&c->e, &known))
        break;
      for (size_t i = 0; i < jh_json_count(cc_root(list)) && cc_ok(c); ++i) {
        assembly_t item = {0};
        if (!assembly_entry(c, jh_json_item(cc_root(list), i), &item))
          continue;
        (void)esp_push(&c->e, &known, jh_span(item.name));
        if (strcmp(item.name, key) != 0)
          continue;
        found = true;
        if (o->target[0] == '\0' || strcmp(item.target, o->target) == 0) {
          *chosen = item;
          chosen->origin = labels[s];
          done = true;
        }
      }
      if (cc_ok(c) && !found) {
        jh_list_sort(&known);
        (void)CC_FAIL(c, jh_span("[JH-HW-ASSEMBLY] "), jh_span(tree_path),
                      jh_span(":/: "), jh_span(labels[s]),
                      jh_span(" selects assembly "),
                      esp_repr(&c->e, jh_span(key)),
                      jh_span(", which the device tree does not declare; "
                              "known: "),
                      esp_repr_list(&c->e, &known));
      }
      jh_list_destroy(&known);
    }
  }
  if (!done && cc_ok(c)) {
    jh_hardware_destroy(&hardware);
    hardware = (jh_hardware_t){0};
    jb_json_tree_t *selection = NULL;
    if (!cc_storage(c, jb_string_assign(&text, "", 0)) ||
        jh_hardware_load(&hardware, &c->e.hardware, tree_path, c->e.failure) !=
            HAL_OK ||
        jh_hardware_select(&hardware, o->target[0] != '\0' ? o->target : NULL,
                           o->assembly[0] != '\0' ? o->assembly : NULL, &text,
                           c->e.failure) != HAL_OK) {
      (void)hardware_failure(c);
      goto out;
    }
    if (cc_storage(c,
                   jh_json_tree_from_text(jh_string_span(&text), &selection)) &&
        assembly_entry(c, cc_root(selection), chosen)) {
      chosen->origin = o->assembly[0] != '\0' ? "explicit" : "automatic";
      done = true;
    }
    jb_json_tree_destroy(selection);
  }
out:
  jb_json_tree_destroy(list);
  jb_string_destroy(&text);
  jh_hardware_destroy(&hardware);
  return cc_ok(c) && done;
}

/* place_assembly_build_dir. */
static bool place_assembly_build_dir(jh_compile_ctx_t *c,
                                     jb_json_tree_t *config,
                                     const assembly_t *chosen) {
  const jb_json_tree_view_t root = cc_root(config);
  jb_json_tree_view_t base = cc_get(root, "buildDir");
  const jb_json_tree_view_t overlay =
      cc_get(cc_get(root, "targetProfiles"), chosen->target);
  if (cc_is(overlay, JB_JSON_OBJECT) && cc_truthy(cc_get(overlay, "buildDir")))
    base = cc_get(overlay, "buildDir");
  const char *text = cc_truthy(base) ? cc_str(c, base) : "${project}/.build";
  if (text == NULL)
    return CC_FAIL(c, jh_span("project manifest field 'buildDir' must be a "
                              "string"));
  /* Decided before the change below invalidates the views into config. */
  const bool drop =
      cc_is(overlay, JB_JSON_OBJECT) && cc_has(overlay, "buildDir");
  cc_set_text(c, config, "buildDir",
              cc_os_join(c, cc_os_join(c, text, chosen->target), chosen->name));
  if (drop) {
    jb_json_tree_t *profiles =
        cc_copy(c, cc_get(cc_root(config), "targetProfiles"));
    jb_json_tree_t *profile =
        cc_copy(c, cc_get(cc_root(profiles), chosen->target));
    cc_erase(c, profile, "buildDir");
    set_tree(c, profiles, chosen->target, profile);
    set_tree(c, config, "targetProfiles", profiles);
  }
  return cc_ok(c);
}

static bool resolve_assembly_profile(jh_compile_ctx_t *c,
                                     jb_json_tree_t **config,
                                     const assembly_t *chosen) {
  cc_set_text(c, *config, "assembly", chosen->name);
  const profile_request_t request = {chosen->target, chosen->board, NULL,
                                     "cli"};
  if (!resolve_target_profile(c, config, &request))
    return false;
  const jb_json_tree_view_t root = cc_root(*config);
  const char *build_dir = cc_pystr(c, cc_get(root, "buildDir"));
  cc_set_text(c, *config, "_assemblyBuildDir", build_dir);
  const char *toolchain = cc_str(c, cc_get(cc_root(*config), "toolchain"));
  if (toolchain != NULL && strcmp(toolchain, "cmake") == 0 &&
      cc_is(cc_get(cc_root(*config), "cmake"), JB_JSON_OBJECT)) {
    jb_json_tree_t *cmake = cc_copy(c, cc_get(cc_root(*config), "cmake"));
    jb_json_tree_t *cache = cc_section(c, cc_root(cmake), "cache");
    const jb_json_tree_view_t artifacts =
        cc_get(cc_root(cache), "JH_ARTIFACT_DIR");
    if (!cc_truthy(artifacts))
      cc_set_text(c, cache, "JH_ARTIFACT_DIR", build_dir);
    else if (!cc_path_within(c, cc_pystr(c, artifacts), build_dir))
      cc_set_text(
          c, cache, "JH_ARTIFACT_DIR",
          cc_os_join(c, cc_os_join(c, cc_pystr(c, artifacts), chosen->target),
                     chosen->name));
    set_tree(c, cmake, "cache", cache);
    set_tree(c, *config, "cmake", cmake);
  }
  const char *origin = strcmp(chosen->origin, "explicit") == 0 ? "cli"
                       : strcmp(chosen->origin, "automatic") == 0
                           ? "only assembly"
                           : chosen->origin;
  const char *source =
      CC_TEXT(c, jh_span("device_tree.json assemblies."), jh_span(chosen->name),
              jh_span(" ("), jh_span(origin), jh_span(")"));
  cc_source(c, "target", source);
  cc_source(c, "board", source);
  cc_source(c, "assembly", source);
  const char *previous = cc_str(c, cc_get(cc_root(c->sources), "buildDir"));
  cc_source(c, "buildDir",
            CC_TEXT(c, jh_span(previous != NULL ? previous : "default"),
                    jh_span(" + <target>/<assembly>")));
  return cc_ok(c);
}

/* ---- foreign_checkout_findings ---- */

bool cc_same_path(const char *a, const char *b) {
#ifdef _WIN32
  return _stricmp(a, b) == 0;
#else
  return strcmp(a, b) == 0;
#endif
}

static bool checkout_findings(jh_compile_ctx_t *c, jb_json_tree_view_t config) {
  jb_string_t out = {0};
  hal_status_t status = jb_string_init(&out, jb_heap_allocator());
  const jb_json_tree_view_t sources = cc_root(c->sources);
  const char *fix = CC_TEXT(c, jh_span("point it at "), jh_span(c->root),
                            jh_span(" or run the jh-vscode of that checkout"));
  const char *root = cc_str(c, cc_get(config, "root"));
  if (root != NULL && root[0] != '\0') {
    const char *resolved =
        esp_resolve(&c->e, esp_expand_user(&c->e, root), c->project_dir);
    if (cc_ok(c) && !cc_same_path(resolved, c->root)) {
      const char *where = cc_str(c, cc_get(sources, "root"));
      jh_put(&out, &status, "[JH-HAL-ROOT] ");
      jh_put(&out, &status, where != NULL ? where : SETTINGS_LABEL);
      jh_put(&out, &status, ": jaszczurhal.root is ");
      jh_put(&out, &status, resolved);
      jh_put(&out, &status, ", but this jh-vscode runs from ");
      jh_put(&out, &status, c->root);
      jh_put(&out, &status, "; ");
      jh_put(&out, &status, fix);
    }
  }
  const char *source_dir =
      cc_str(c, cc_get(cc_get(config, "cmake"), "sourceDir"));
  if (!cc_truthy(cc_get(cc_get(config, "cmake"), "sourceDir")) ||
      !cc_is(cc_get(config, "cmake"), JB_JSON_OBJECT))
    source_dir = c->project_dir;
  else if (source_dir == NULL)
    source_dir = cc_pystr(c, cc_get(cc_get(config, "cmake"), "sourceDir"));
  const char *source =
      esp_resolve(&c->e, cc_project_absolute(c, source_dir), c->project_dir);
  const char *parent = esp_parent(&c->e, source);
  const char *checkout = esp_parent(&c->e, parent);
  if (cc_ok(c) && strcmp(name_of(source), "jh_firmware_project") == 0 &&
      strcmp(name_of(parent), "cmake") == 0 &&
      !cc_same_path(checkout, c->root)) {
    const char *where = cc_str(c, cc_get(sources, "cmake"));
    if (jb_string_size(&out) != 0)
      jh_put(&out, &status, "\n");
    jh_put(&out, &status, "[JH-HAL-ROOT] ");
    jh_put(&out, &status, where != NULL ? where : MANIFEST_LABEL);
    jh_put(&out, &status, ": cmake.sourceDir is the dispatcher of ");
    jh_put(&out, &status, checkout);
    jh_put(&out, &status, ", but this jh-vscode runs from ");
    jh_put(&out, &status, c->root);
    jh_put(&out, &status, "; use ${jhRoot}/cmake/jh_firmware_project");
  }
  if (cc_storage(c, status) && jb_string_size(&out) != 0)
    (void)CC_FAIL(c, jh_string_span(&out));
  jb_string_destroy(&out);
  return cc_ok(c);
}

/* ---- load_project_config ---- */

static void setting(jh_compile_ctx_t *c, jb_json_tree_t *config,
                    jb_json_tree_view_t settings, const char *semantic,
                    const char *key) {
  if (cc_has(cc_root(config), key) &&
      (strcmp(key, "project") == 0 || strcmp(key, "module") == 0 ||
       strcmp(key, "buildDir") == 0))
    return;
  const jb_json_tree_view_t value =
      cc_get(settings, CC_TEXT(c, jh_span("jaszczurhal."), jh_span(semantic)));
  if (value.tree == NULL || cc_is(value, JB_JSON_NULL))
    return;
  cc_set(c, config, key, value);
  cc_source(c, key, SETTINGS_LABEL);
}

static bool identity_settings(jh_compile_ctx_t *c, jb_json_tree_t *config,
                              jb_json_tree_view_t settings) {
  const jb_json_tree_view_t current = cc_get(cc_root(config), "identity");
  if (cc_truthy(current) && !cc_is(current, JB_JSON_OBJECT))
    return CC_FAIL(c, jh_span("project manifest field 'identity' must be an "
                              "object"));
  jb_json_tree_t *identity =
      cc_truthy(current) ? cc_copy(c, current) : cc_object(c);
  static const char *const semantic[] = {"identityEnabled", "usbManufacturer",
                                         "usbProduct"};
  static const char *const keys[] = {"enabled", "usbManufacturer",
                                     "usbProduct"};
  for (size_t i = 0; i < 3 && cc_ok(c); ++i) {
    if (cc_has(cc_root(identity), keys[i]))
      continue;
    const jb_json_tree_view_t value = cc_get(
        settings, CC_TEXT(c, jh_span("jaszczurhal."), jh_span(semantic[i])));
    if (value.tree == NULL || cc_is(value, JB_JSON_NULL))
      continue;
    cc_set(c, identity, keys[i], value);
    cc_source(c, CC_TEXT(c, jh_span("identity."), jh_span(keys[i])),
              SETTINGS_LABEL);
  }
  if (cc_ok(c) && jh_json_count(cc_root(identity)) != 0) {
    int32_t vid = 0, pid = 0;
    if (identity_ids(c, cc_root(identity),
                     "invalid USB identity configuration: ", &vid, &pid))
      cc_set(c, config, "identity", cc_root(identity));
  }
  jb_json_tree_destroy(identity);
  return cc_ok(c);
}

static void local_upload(jh_compile_ctx_t *c, jb_json_tree_t *config,
                         jb_json_tree_view_t local) {
  const jb_json_tree_view_t port = cc_get(local, "uploadPort");
  if (cc_truthy(port)) {
    const char *text = cc_pystr(c, port);
    cc_set_text(c, config, "uploadPort", text);
    jb_json_tree_t *upload = cc_section(c, cc_root(config), "upload");
    cc_set_text(c, upload, "port", text);
    set_tree(c, config, "upload", upload);
    cc_source(c, "uploadPort", LOCAL_LABEL);
  }
  const jb_json_tree_view_t volume = cc_get(local, "bootselVolume");
  if (cc_truthy(volume)) {
    jb_json_tree_t *upload = cc_section(c, cc_root(config), "upload");
    cc_set_text(c, upload, "bootselVolume", cc_pystr(c, volume));
    set_tree(c, config, "upload", upload);
    cc_source(c, "upload.bootselVolume", LOCAL_LABEL);
  }
}

static bool load_project_config(jh_compile_ctx_t *c, jb_json_tree_t **out) {
  const char *vscode = esp_join(&c->e, c->project_dir, ".vscode");
  const char *manifest_path =
      esp_join(&c->e, vscode, "jaszczurhal.project.json");
  jb_json_tree_t *manifest = load_json(c, manifest_path);
  jb_json_tree_t *settings =
      load_json(c, esp_join(&c->e, vscode, "settings.json"));
  jb_json_tree_t *local = NULL, *config = NULL;
  jb_vector_t findings = {0};
  if (!cc_ok(c) || !esp_list_init(&c->e, &findings) ||
      jh_manifest_findings(cc_root(manifest), jh_span(MANIFEST_LABEL),
                           &findings, c->e.failure) != HAL_OK)
    goto done;
  if (jb_vector_size(&findings) != 0) {
    (void)CC_FAIL(c, jh_span(esp_join_list(&c->e, &findings, "\n")));
    goto done;
  }
  config = cc_object(c);
  static const char *const known[] = {
      "project",  "module",         "toolchain", "target",        "board",
      "assembly", "targetProfiles", "buildDir",  "cmakeBuildDir", "cmake",
      "identity", "artifacts",      "espIdf",    "upload",        "ota",
      "hooks"};
  for (size_t i = 0; i < sizeof known / sizeof *known && cc_ok(c); ++i)
    if (cc_has(cc_root(manifest), known[i])) {
      cc_set(c, config, known[i], cc_get(cc_root(manifest), known[i]));
      cc_source(c, known[i], MANIFEST_LABEL);
    }
  static const char *const semantic[] = {
      "uploadPort", "buildDir",    "verbose", "projectName",
      "moduleName", "vscodeEntry", "root"};
  static const char *const keys[] = {"uploadPort", "buildDir", "verbose",
                                     "project",    "module",   "vscodeEntry",
                                     "root"};
  for (size_t i = 0; i < 7 && cc_ok(c); ++i)
    setting(c, config, cc_root(settings), semantic[i], keys[i]);
  if (!identity_settings(c, config, cc_root(settings)))
    goto done;
  if (!cc_has(cc_root(config), "project")) {
    cc_set_text(c, config, "project",
                name_of(esp_parent(&c->e, c->project_dir)));
    cc_source(c, "project", "directory-fallback");
  }
  if (!cc_has(cc_root(config), "module")) {
    cc_set_text(c, config, "module", name_of(c->project_dir));
    cc_source(c, "module", "directory-fallback");
  }
  if (!cc_has(cc_root(config), "toolchain")) {
    cc_set_text(c, config, "toolchain", "custom");
    cc_source(c, "toolchain", "directory-fallback");
  }
  if (!cc_has(cc_root(config), "buildDir")) {
    cc_set_text(c, config, "buildDir",
                cc_path_join(c, c->project_dir, ".build"));
    cc_source(c, "buildDir", "default");
  }
  local = load_json(c, esp_join(&c->e, vscode, "jaszczurhal.local.json"));
  if (!cc_ok(c))
    goto done;
  const char *tree_path = esp_join(&c->e, c->project_dir, "device_tree.json");
  c->device_tree = esp_is_file(tree_path);
  assembly_t chosen = {0};
  if (c->device_tree && (!choose_assembly(c, cc_root(config), cc_root(local),
                                          tree_path, &chosen) ||
                         !place_assembly_build_dir(c, config, &chosen)))
    goto done;
  expand_sections(c, config, NULL);
  local_upload(c, config, cc_root(local));
  if (!cc_ok(c))
    goto done;
  if (c->device_tree) {
    if (!resolve_assembly_profile(c, &config, &chosen))
      goto done;
  } else {
    const char *where[] = {"--assembly", MANIFEST_LABEL};
    const char *values[] = {
        c->options->assembly[0] != '\0' ? c->options->assembly : NULL,
        cc_truthy(cc_get(cc_root(config), "assembly"))
            ? cc_pystr(c, cc_get(cc_root(config), "assembly"))
            : NULL};
    for (size_t i = 0; i < 2; ++i)
      if (values[i] != NULL) {
        (void)CC_FAIL(c, jh_span("[JH-HW-ASSEMBLY] "), jh_span(where[i]),
                      jh_span(": assembly "),
                      esp_repr(&c->e, jh_span(values[i])), jh_span(" needs "),
                      jh_span(tree_path));
        goto done;
      }
    const char *local_target =
        cc_truthy(cc_get(cc_root(local), "target"))
            ? cc_pystr(c, cc_get(cc_root(local), "target"))
            : NULL;
    const jb_json_tree_view_t local_board = cc_get(cc_root(local), "board");
    const profile_request_t request = {
        c->options->target[0] != '\0' ? c->options->target : local_target,
        c->options->board[0] != '\0' ? c->options->board : NULL,
        local_board.tree != NULL && !cc_is(local_board, JB_JSON_NULL)
            ? cc_pystr(c, local_board)
            : NULL,
        c->options->target[0] != '\0' ? "cli" : LOCAL_LABEL};
    if (!resolve_target_profile(c, &config, &request))
      goto done;
  }
  cc_set_text(c, config, "_projectDir", c->project_dir);
  (void)checkout_findings(c, cc_root(config));
done:
  jh_list_destroy(&findings);
  jb_json_tree_destroy(local);
  jb_json_tree_destroy(settings);
  jb_json_tree_destroy(manifest);
  if (!cc_ok(c)) {
    jb_json_tree_destroy(config);
    return false;
  }
  *out = config;
  return true;
}

/* ---- apply_variant ---- */

static bool apply_variant(jh_compile_ctx_t *c, jb_json_tree_t *config) {
  const char *variant = c->options->variant;
  if (variant[0] == '\0')
    return cc_ok(c);
  const char *header = esp_join(&c->e, c->project_dir, "hal_project_config.h");
  jh_project_config_t declared;
  jh_project_config_init(&declared);
  bool known = false;
  jb_vector_t ids = {0};
  if (cc_ok(c) && esp_list_init(&c->e, &ids) &&
      jh_project_config_read(header, NULL, "", NULL, &declared, c->e.failure) ==
          HAL_OK) {
    for (size_t i = 0; i < jb_vector_size(&declared.variants); ++i) {
      const jh_config_variant_t *item =
          jb_vector_const_at(&declared.variants, i);
      (void)esp_push(&c->e, &ids, jh_string_span(&item->id));
      known = known || strcmp(jb_string_data(&item->id), variant) == 0;
    }
  } else if (c->e.failure->status != HAL_ENOMEM) {
    c->e.failure->exit_code = 2;
  }
  jh_project_config_destroy(&declared);
  if (cc_ok(c) && !known) {
    if (jb_vector_size(&ids) != 0)
      (void)CC_FAIL(c, jh_span("unknown variant "),
                    esp_repr(&c->e, jh_span(variant)),
                    jh_span("; known variants: "),
                    jh_span(esp_join_list(&c->e, &ids, ", ")));
    else
      (void)CC_FAIL(c, jh_span("unknown variant "),
                    esp_repr(&c->e, jh_span(variant)));
  }
  jh_list_destroy(&ids);
  if (!cc_ok(c))
    return false;
  const jb_json_tree_view_t root = cc_root(config);
  const jb_json_tree_view_t module_value = cc_get(root, "module");
  const char *module =
      CC_TEXT(c,
              jh_span(module_value.tree != NULL ? cc_pystr(c, module_value)
                                                : "firmware"),
              jh_span("_"), jh_span(variant));
  cc_set_text(c, config, "module", module);
  const jb_json_tree_view_t base_value = cc_get(cc_root(config), "buildDir");
  const char *base = cc_truthy(base_value) ? cc_pystr(c, base_value) : "";
  if (base[0] != '\0') {
    const char *variant_dir =
        cc_path_join(c, cc_path_join(c, base, "variants"), variant);
    cc_set_text(c, config, "buildDir", variant_dir);
    const jb_json_tree_view_t artifacts = cc_get(cc_root(config), "artifacts");
    if (cc_is(artifacts, JB_JSON_OBJECT)) {
      jb_json_tree_t *rebased = cc_copy(c, artifacts);
      const char *pure_base = cc_purepath(c, base);
      for (size_t i = 0; i < jh_json_count(cc_root(rebased)) && cc_ok(c); ++i) {
        const jb_json_tree_view_t item = jh_json_item(cc_root(rebased), i);
        const char *value = cc_str(c, item);
        if (value == NULL)
          continue;
        const char *pure = cc_purepath(c, value);
        const size_t length = strlen(pure_base);
        const bool root_base = strcmp(pure_base, CC_SEPARATOR) == 0;
        if (strcmp(pure, pure_base) == 0)
          cc_set_text(c, rebased, CC_TEXT(c, jh_json_key(item)), variant_dir);
        else if (strncmp(pure, pure_base, length) == 0 &&
                 (root_base || jh_build_separator(pure[length])))
          cc_set_text(c, rebased, CC_TEXT(c, jh_json_key(item)),
                      cc_path_join(c, variant_dir,
                                   pure + length + (root_base ? 0 : 1)));
      }
      set_tree(c, config, "artifacts", rebased);
    }
  }
  const jb_json_tree_view_t cmake_dir =
      cc_get(cc_root(config), "cmakeBuildDir");
  if (cc_truthy(cmake_dir))
    cc_set_text(c, config, "cmakeBuildDir",
                CC_TEXT(c, jh_span(cc_pystr(c, cmake_dir)),
                        jh_span("/variants/"), jh_span(variant)));
  const jb_json_tree_view_t cmake_value = cc_get(cc_root(config), "cmake");
  if (cc_truthy(cmake_value) && !cc_is(cmake_value, JB_JSON_OBJECT))
    return CC_FAIL(c, jh_span("project manifest field 'cmake' must be an "
                              "object"));
  jb_json_tree_t *cmake = cc_section(c, cc_root(config), "cmake");
  const jb_json_tree_view_t cache_value = cc_get(cc_root(cmake), "cache");
  if (cc_truthy(cache_value) && !cc_is(cache_value, JB_JSON_OBJECT)) {
    jb_json_tree_destroy(cmake);
    return CC_FAIL(c, jh_span("project manifest field 'cmake.cache' must be "
                              "an object"));
  }
  jb_json_tree_t *cache = cc_section(c, cc_root(cmake), "cache");
  const jb_json_tree_view_t artifact_dir =
      cc_get(cc_root(cache), "JH_ARTIFACT_DIR");
  if (cc_truthy(artifact_dir) && strcmp(cc_pystr(c, artifact_dir), base) != 0)
    cc_set_text(
        c, cache, "JH_ARTIFACT_DIR",
        cc_path_join(c, cc_path_join(c, cc_pystr(c, artifact_dir), "variants"),
                     variant));
  else if (base[0] != '\0')
    cc_set(c, cache, "JH_ARTIFACT_DIR", cc_get(cc_root(config), "buildDir"));
  cc_set_text(c, cache, "JH_MODULE_NAME", module);
  cc_set_text(c, cache, "JH_VARIANT", variant);
  set_tree(c, cmake, "cache", cache);
  set_tree(c, config, "cmake", cmake);
  cc_set_text(c, config, "variant", variant);
  cc_source(c, "variant", "cli");
  return cc_ok(c);
}

/* ---- apply_cli_overrides ---- */

static void apply_cli_overrides(jh_compile_ctx_t *c, jb_json_tree_t *config) {
  const jh_compile_options_t *o = c->options;
  if (o->port[0] != '\0') {
    cc_set_text(c, config, "uploadPort", o->port);
    jb_json_tree_t *upload = cc_section(c, cc_root(config), "upload");
    cc_set_text(c, upload, "port", o->port);
    set_tree(c, config, "upload", upload);
    cc_source(c, "uploadPort", "cli");
  }
  if (o->bootsel_volume[0] != '\0') {
    jb_json_tree_t *upload = cc_section(c, cc_root(config), "upload");
    cc_set_text(c, upload, "bootselVolume", o->bootsel_volume);
    set_tree(c, config, "upload", upload);
    cc_source(c, "upload.bootselVolume", "cli");
  }
  if (o->verbose) {
    set_tree(c, config, "verbose", cc_bool(c, true));
    cc_source(c, "verbose", "cli");
  }
}

/* ---- attach_hal_feature_resolution ---- */

static bool hardware_header(jh_compile_ctx_t *c, const char *assembly,
                            const char **path, jb_temp_dir_t **scratch) {
  if (!hardware_registry(c))
    return false;
  jh_hardware_t hardware = {0};
  jb_string_t header = {0}, board = {0};
  jb_json_tree_t *board_tree = NULL;
  const char *tree_path = esp_join(&c->e, c->project_dir, "device_tree.json");
  if (cc_storage(c, jb_string_init(&header, jb_heap_allocator())) &&
      cc_storage(c, jb_string_init(&board, jb_heap_allocator())) &&
      jh_hardware_load(&hardware, &c->e.hardware, tree_path, c->e.failure) ==
          HAL_OK &&
      jh_hardware_resolve(&hardware, NULL, assembly, c->e.failure) == HAL_OK &&
      jh_hardware_header(&hardware, &header, c->e.failure) == HAL_OK &&
      jh_hardware_board_json(&hardware, &board, c->e.failure) == HAL_OK &&
      jh_process_scratch(scratch, c->e.failure) == HAL_OK) {
    *path = esp_join(&c->e, jb_temp_dir_path(*scratch), "jh_hardware.h");
    bool changed = false;
    (void)cc_storage(c, jb_file_write_atomic(*path, jb_string_data(&header),
                                             jb_string_size(&header), false,
                                             &changed));
    /* device_tree_project.components: the assembly board's components, which
     * the build preflight checks for the CYW43 transport. */
    jb_json_tree_view_t root = {0};
    if (cc_ok(c) &&
        cc_storage(
            c, jh_json_tree_from_text(jh_string_span(&board), &board_tree)) &&
        cc_storage(c, jb_json_tree_root(board_tree, &root))) {
      const jb_json_tree_view_t names = cc_get(root, "components");
      for (size_t i = 0; i < jh_json_count(names) && cc_ok(c); ++i) {
        jb_json_span_t name = {0};
        if (jh_json_string(jh_json_item(names, i), &name))
          (void)esp_push(&c->e, &c->assembly_components, name);
      }
    }
  } else {
    (void)hardware_failure(c);
  }
  jb_json_tree_destroy(board_tree);
  jb_string_destroy(&board);
  jb_string_destroy(&header);
  jh_hardware_destroy(&hardware);
  return cc_ok(c);
}

static void list_tree(jh_compile_ctx_t *c, jb_json_tree_t *object,
                      const char *key, const jb_vector_t *list) {
  jb_json_tree_t *array = cc_array(c);
  for (size_t i = 0; i < jb_vector_size(list) && cc_ok(c); ++i)
    cc_append_text(c, array, esp_at(list, i));
  set_tree(c, object, key, array);
}

static bool feature_resolution(jh_compile_ctx_t *c, jb_json_tree_t *config) {
  if (!load_registries(c))
    return false;
  const jb_json_tree_view_t root = cc_root(config);
  const char *target = cc_truthy(cc_get(root, "target"))
                           ? cc_pystr(c, cc_get(root, "target"))
                           : "";
  const char *selector = "";
  jb_vector_t required = {0}, extra = {0};
  jh_project_build_t build;
  jh_project_build_init(&build);
  jh_feature_resolution_t resolution;
  jh_feature_resolution_init(&resolution);
  jb_temp_dir_t *scratch = NULL;
  const char *hardware = "";
  jb_json_span_t *required_spans = NULL;
  if (!esp_list_init(&c->e, &required) || !esp_list_init(&c->e, &extra))
    goto done;
  if (target[0] != '\0') {
    const jh_board_descriptor_t *descriptor =
        jh_board_registry_find(&c->e.boards.targets, jh_span(target));
    if (descriptor == NULL) {
      (void)CC_FAIL(c, jh_span("[JH-CFG-TARGET] unknown target "),
                    esp_repr(&c->e, jh_span(target)));
      goto done;
    }
    selector = CC_TEXT(
        c, jh_build_descriptor_text(descriptor->root, "hal", "targetSelector"));
    const jb_json_tree_view_t needed =
        cc_get(descriptor->root, "requiredFeatures");
    for (size_t i = 0; i < jh_json_count(needed); ++i) {
      jb_json_span_t feature = {0};
      if (jh_json_string(jh_json_item(needed, i), &feature))
        (void)esp_push(&c->e, &required, feature);
    }
  }
  const char *assembly = cc_truthy(cc_get(root, "assembly"))
                             ? cc_pystr(c, cc_get(root, "assembly"))
                             : NULL;
  if (assembly != NULL && !hardware_header(c, assembly, &hardware, &scratch))
    goto done;
  const char *header = cc_path_join(c, c->project_dir, "hal_project_config.h");
  const char *variant = cc_truthy(cc_get(root, "variant"))
                            ? cc_pystr(c, cc_get(root, "variant"))
                            : "";
  const jh_project_build_options_t options = {
      header, hardware, target, selector, variant, &required, &extra};
  if (!cc_ok(c))
    goto done;
  if (jh_project_build_evaluate(&options, &build, c->e.failure) != HAL_OK) {
    if (c->e.failure->status != HAL_ENOMEM)
      c->e.failure->exit_code = 2;
    goto done;
  }
  /* project_config.defined() of the three symbols the build preflight
   * accepts as the STM32G474 CYW43 gSPI/lwIP profile. */
  {
    static const char *const cyw43[] = {"HAL_NETWORK_BACKEND_CYW43",
                                        "HAL_CYW43_BUS_STM32_GSPI",
                                        "HAL_CYW43_STACK_LWIP"};
    const jb_json_tree_view_t macros = cc_get(build.config_root, "macros");
    size_t found = 0;
    for (size_t k = 0; k < COUNTOF(cyw43); ++k)
      for (size_t i = 0; i < jh_json_count(macros); ++i) {
        jb_json_span_t name = {0};
        if (jh_json_string(cc_get(jh_json_item(macros, i), "name"), &name) &&
            jh_span_equal(name, jh_span(cyw43[k]))) {
          ++found;
          break;
        }
      }
    c->cyw43_defined = found == COUNTOF(cyw43);
  }
  /* The context of the findings: "<header> [<label>]". */
  const char *label = CC_TEXT(
      c, jh_span("target="), jh_span(target[0] != '\0' ? target : "default"),
      jh_span(assembly != NULL ? ", assembly=" : ""),
      jh_span(assembly != NULL ? assembly : ""), jh_span(", variant="),
      jh_span(variant[0] != '\0' ? variant : "base"));
  const char *context =
      CC_TEXT(c, jh_span(header), jh_span(" ["), jh_span(label), jh_span("]"));
  const size_t count = jb_vector_size(&required);
  if (count != 0) {
    required_spans = calloc(count, sizeof *required_spans);
    if (required_spans == NULL) {
      (void)cc_storage(c, HAL_ENOMEM);
      goto done;
    }
    for (size_t i = 0; i < count; ++i)
      required_spans[i] = jh_span(esp_at(&required, i));
  }
  const jh_feature_target_t facts = {jh_span(target), required_spans, count};
  if (!cc_ok(c) ||
      jh_feature_resolve(&c->e.features, jb_vector_const_at(&build.requests, 0),
                         jb_vector_size(&build.requests), jh_span(context),
                         target[0] != '\0' ? &facts : NULL, &resolution,
                         c->e.failure) != HAL_OK)
    goto done;
  if (jb_vector_size(&resolution.findings) != 0) {
    jh_list_sort(&resolution.findings);
    jb_vector_t unique = {0};
    if (esp_list_init(&c->e, &unique)) {
      for (size_t i = 0; i < jb_vector_size(&resolution.findings); ++i)
        (void)esp_push_unique(
            &c->e, &unique,
            jh_string_span(jh_list_at(&resolution.findings, i)));
      (void)CC_FAIL(c, jh_span(esp_join_list(&c->e, &unique, "\n")));
    }
    jh_list_destroy(&unique);
    goto done;
  }
  jb_json_tree_t *object = cc_object(c);
  cc_set_text(c, object, "registryDigest", c->e.features.digest);
  list_tree(c, object, "requestedFeatures", &resolution.requested);
  list_tree(c, object, "resolvedFeatures", &resolution.resolved);
  cc_set_text(c, object, "resolvedFeaturesDigest", resolution.digest);
  jb_json_tree_t *provenance = cc_object(c);
  for (size_t i = 0; i < jb_vector_size(&resolution.provenance) && cc_ok(c);
       ++i) {
    const jh_feature_provenance_t *item =
        jb_vector_const_at(&resolution.provenance, i);
    list_tree(c, provenance, jb_string_data(&item->symbol), &item->sources);
  }
  set_tree(c, object, "provenance", provenance);
  set_tree(c, config, "featureResolution", object);
  for (size_t i = 0; i < jb_vector_size(&resolution.resolved) && cc_ok(c); ++i)
    (void)esp_push(&c->e, &c->resolved_features,
                   jh_string_span(jh_list_at(&resolution.resolved, i)));
done:
  free(required_spans);
  if (scratch != NULL)
    (void)jb_temp_dir_remove(&scratch);
  jh_feature_resolution_destroy(&resolution);
  jh_project_build_destroy(&build);
  jh_list_destroy(&extra);
  jh_list_destroy(&required);
  return cc_ok(c);
}

bool cc_model(jh_compile_ctx_t *c, jb_json_tree_t **out) {
  jb_json_tree_t *config = NULL;
  c->sources = cc_object(c);
  if (!cc_ok(c) || !load_project_config(c, &config))
    return false;
  if (apply_variant(c, config)) {
    apply_cli_overrides(c, config);
    cc_set(c, config, "_sources", cc_root(c->sources));
    (void)feature_resolution(c, config);
  }
  if (!cc_ok(c)) {
    jb_json_tree_destroy(config);
    return false;
  }
  *out = config;
  return true;
}

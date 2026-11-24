/* The build plan of jh compile: the trees, commands and environment that
 * command_build of vscode/runtime/jh_vscode.py derives from the model, without
 * running them (jh_compile.h). C11. */
#include "jh_build_support.h"
#include "jh_compile_internal.h"
#include "jh_json_check.h"
#include "jh_json_digest.h"
#include "jh_json_file.h"
#include "jh_process_support.h"
#include "jh_python_repr.h"
#include "jh_usb_identifier.h"
#include <jb/sha256.h>
#include <string.h>

#define CACHE_KEYS_FILE ".jh-vscode-cache-keys.json"

/* Keys a tree configured before the key record existed may still hold. */
static const char *const transient_keys[] = {
    "JH_ARTIFACT_DIR",     "JH_EXTRA_INCLUDES", "JH_EXTRA_LIBRARIES",
    "JH_EXTRA_SOURCES",    "JH_LINK_LIBRARIES", "JH_PROJECT_RECIPE",
    "JH_VARIANT",          "JH_OTA_GENERATION", "JH_OTA_VERSION",
    "JH_USB_MANUFACTURER", "JH_USB_PRODUCT",    "JH_USB_VID",
    "JH_USB_PID"};

static const char *const native_rp_targets[] = {"rp2040", "rp2350-arm",
                                                "rp2350-riscv"};

static bool native_rp(const char *target) {
  for (size_t i = 0; i < 3; ++i)
    if (target != NULL && strcmp(target, native_rp_targets[i]) == 0)
      return true;
  return false;
}

static const char *config_text(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                               const char *key) {
  const jb_json_tree_view_t value = cc_get(config, key);
  return cc_truthy(value) ? cc_pystr(c, value) : NULL;
}

/* ---- Windows host environment ---- */

static bool windows_host(void) {
#ifdef _WIN32
  return true;
#else
  jb_string_t value = {0};
  bool found = false;
  bool result = false;
  if (jb_string_init(&value, jb_heap_allocator()) == HAL_OK &&
      jh_process_environment("JH_TEST_WINDOWS_HOST", &value, &found) ==
          HAL_OK &&
      found)
    result = strcmp(jb_string_data(&value), "1") == 0;
  jb_string_destroy(&value);
  return result;
#endif
}

static const char *environment_value(jh_compile_ctx_t *c, const char *name) {
  jb_string_t value = {0};
  bool found = false;
  const char *result = NULL;
  if (cc_storage(c, jb_string_init(&value, jb_heap_allocator())) &&
      cc_storage(c, jh_process_environment(name, &value, &found)) && found)
    result = CC_TEXT(c, jh_string_span(&value));
  jb_string_destroy(&value);
  return result;
}

/* load_windows_host_environment: {} off Windows. */
static bool load_host(jh_compile_ctx_t *c) {
  if (c->host != NULL || !cc_ok(c))
    return cc_ok(c);
  if (!windows_host()) {
    c->host = cc_object(c);
    return cc_ok(c);
  }
  const char *override = environment_value(c, "JH_WINDOWS_HOST_ENVIRONMENT");
  const char *path =
      override != NULL && override[0] != '\0'
          ? esp_expand_user(&c->e, override)
          : esp_join(
                &c->e,
                esp_join(&c->e, esp_join(&c->e, c->root, ".build"), "windows"),
                "host-environment.json");
  jh_json_location_t where;
  jh_json_location_init(&where);
  jh_failure_t error;
  jh_failure_init(&error);
  bool exists = false;
  const hal_status_t status = jh_json_read_settings(
      &where, path, 16u * 1024u * 1024u, &c->host, &exists, &error);
  if (status == HAL_ENOMEM)
    (void)cc_storage(c, status);
  else if (status == HAL_ETYPE)
    (void)CC_FAIL(c, jh_span(path), jh_span(" must contain a JSON object"));
  else if (status != HAL_OK)
    (void)CC_FAIL(c, jh_span("invalid JSON in "), jh_span(path), jh_span(": "),
                  jh_string_span(&error.message));
  jh_failure_destroy(&error);
  jh_json_location_destroy(&where);
  return cc_ok(c);
}

static const char *managed_root(jh_compile_ctx_t *c) {
  const char *root = cc_str(c, cc_get(cc_root(c->host), "buildRoot"));
  return root != NULL && root[0] != '\0' ? root : NULL;
}

/* resolved_windows_tools: name -> path of every non-empty string. */
static const char *host_tool(jh_compile_ctx_t *c, const char *name) {
  const char *tool = cc_str(c, cc_get(cc_get(cc_root(c->host), "tools"), name));
  return tool != NULL && tool[0] != '\0' ? tool : NULL;
}

static bool has_host_tools(jh_compile_ctx_t *c) {
  const jb_json_tree_view_t tools = cc_get(cc_root(c->host), "tools");
  for (size_t i = 0; cc_is(tools, JB_JSON_OBJECT) && i < jh_json_count(tools);
       ++i) {
    jb_json_span_t text = {0};
    if (jh_json_string(jh_json_item(tools, i), &text) && text.length != 0)
      return true;
  }
  return false;
}

/* windows_project_build_key: <stem[:24]>-<sha256(normcase(path))[:12]>. */
static const char *project_build_key(jh_compile_ctx_t *c) {
  const char *resolved = esp_resolve(&c->e, c->project_dir, c->base);
  jb_string_t normalized = {0};
  if (!cc_storage(c, jb_string_init(&normalized, jb_heap_allocator())) ||
      !cc_storage(c, jh_append(&normalized, resolved)))
    return "";
#ifdef _WIN32
  /* ntpath.normcase: / becomes \ and letters lower case. */
  char *data = jb_string_mutable_data(&normalized);
  for (size_t i = 0; i < jb_string_size(&normalized); ++i)
    if (data[i] == '/')
      data[i] = '\\';
    else if (jh_ascii_is_upper(data[i]))
      data[i] = (char)(data[i] - 'A' + 'a');
#endif
  uint8_t digest[32];
  char hex[65];
  jb_sha256_context_t sha = {0};
  jb_sha256_init(&sha);
  jb_sha256_update(&sha, (const uint8_t *)jb_string_data(&normalized),
                   jb_string_size(&normalized));
  jb_sha256_final(&sha, digest);
  jh_sha256_hex(digest, hex);
  jb_string_destroy(&normalized);
  const char *name = esp_name(c->project_dir);
  jb_string_t stem = {0};
  hal_status_t status = jb_string_init(&stem, jb_heap_allocator());
  bool replaced = false;
  for (const char *p = name; *p && status == HAL_OK; ++p) {
    const char ch = *p;
    const bool keep = jh_ascii_is_upper(ch) || jh_ascii_is_lower(ch) ||
                      jh_ascii_is_digit(ch) || ch == '_' || ch == '.' ||
                      ch == '-';
    if (keep) {
      status = jb_string_append(&stem, &ch, 1);
      replaced = false;
    } else if (!replaced) {
      status = jb_string_append(&stem, "-", 1);
      replaced = true;
    }
  }
  const char *text = cc_storage(c, status) ? jb_string_data(&stem) : "";
  size_t begin = 0, end = strlen(text);
  while (begin < end && (text[begin] == '-' || text[begin] == '.'))
    ++begin;
  while (end > begin && (text[end - 1] == '-' || text[end - 1] == '.'))
    --end;
  if (end - begin > 24)
    end = begin + 24;
  const char *key =
      CC_TEXT(c,
              end > begin ? (jb_json_span_t){text + begin, end - begin}
                          : jh_span("firmware"),
              jh_span("-"), ((jb_json_span_t){hex, 12}));
  jb_string_destroy(&stem);
  return key;
}

/* ---- Directories ---- */

/* get_build_dir: the artifact directory. */
static const char *artifact_dir(jh_compile_ctx_t *c,
                                jb_json_tree_view_t config) {
  const char *build = config_text(c, config, "buildDir");
  return cc_project_absolute(
      c, build != NULL ? build : cc_path_join(c, c->project_dir, ".build"));
}

static const char *source_dir(jh_compile_ctx_t *c, jb_json_tree_view_t config) {
  const jb_json_tree_view_t cmake = cc_get(config, "cmake");
  const jb_json_tree_view_t source = cc_is(cmake, JB_JSON_OBJECT)
                                         ? cc_get(cmake, "sourceDir")
                                         : (jb_json_tree_view_t){0};
  return cc_project_absolute(c, cc_truthy(source) ? cc_pystr(c, source)
                                                  : c->project_dir);
}

/* get_cmake_build_dir, with the variant below a Windows buildRoot. */
static const char *cmake_build_dir(jh_compile_ctx_t *c,
                                   jb_json_tree_view_t config, bool debug) {
  const char *managed = managed_root(c);
  const char *dir = NULL;
  if (managed != NULL) {
    dir = cc_path_join(
        c,
        cc_path_join(c, esp_expand_user(&c->e, managed), project_build_key(c)),
        "cmake");
    const char *variant = config_text(c, config, "variant");
    if (variant != NULL)
      dir = cc_path_join(c, cc_path_join(c, dir, "variants"), variant);
  } else {
    const char *configured = config_text(c, config, "cmakeBuildDir");
    dir = cc_project_absolute(
        c, configured != NULL
               ? configured
               : cc_path_join(c, cc_path_join(c, c->project_dir, ".build"),
                              "cmake"));
  }
  const char *target = config_text(c, config, "target");
  const char *assembly_root = config_text(c, config, "_assemblyBuildDir");
  if (target != NULL &&
      !(assembly_root != NULL && cc_path_within(c, dir, assembly_root))) {
    dir = cc_path_join(c, dir, target);
    const char *board = config_text(c, config, "assembly");
    if (board == NULL)
      board = config_text(c, config, "board");
    if (board != NULL)
      dir = cc_path_join(c, dir, board);
  }
  if (debug)
    dir = cc_path_join(c, dir, "debug");
  return dir;
}

/* managed_build_dir_allowed. */
static bool managed_allowed(jh_compile_ctx_t *c, const char *dir) {
  if (cc_path_within(c, dir, c->project_dir) ||
      cc_path_within(c, dir, esp_join(&c->e, c->root, ".build")))
    return true;
  const char *managed = managed_root(c);
  return managed != NULL &&
         cc_path_within(c, dir, esp_expand_user(&c->e, managed));
}

/* ---- CMake cache ---- */

static const char *cache_value(jh_compile_ctx_t *c, const char *text,
                               const char *key) {
  const size_t length = strlen(key);
  for (const char *line = text; line != NULL && *line;) {
    const char *end = line;
    while (*end && *end != '\n' && *end != '\r')
      ++end;
    if (strncmp(line, key, length) == 0 && line[length] == ':') {
      const char *equal = memchr(line, '=', (size_t)(end - line));
      if (equal != NULL) {
        jb_json_span_t value = {equal + 1, (size_t)(end - equal - 1)};
        (void)jh_python_strip(value, &value);
        return value.length != 0 ? CC_TEXT(c, value) : NULL;
      }
    }
    line = end;
    if (*line == '\r' && line[1] == '\n')
      line += 2;
    else if (*line)
      ++line;
  }
  return NULL;
}

/* reset_stale_cmake_cache_if_needed, as a decision. */
static void reset_decision(jh_compile_ctx_t *c, jb_json_tree_t *plan,
                           const char *source, const char *build,
                           const char *generator) {
  const char *cache_path = esp_join(&c->e, build, "CMakeCache.txt");
  jb_file_data_t data = {0};
  if (!cc_ok(c) || !esp_is_file(cache_path) ||
      jb_file_read(cache_path, 64u * 1024u * 1024u, &data) != HAL_OK)
    return;
  const char *text =
      CC_TEXT(c, ((jb_json_span_t){(const char *)data.data, data.length}));
  jb_file_release(&data);
  const char *home = cache_value(c, text, "CMAKE_HOME_DIRECTORY");
  if (home == NULL)
    return;
  const char *cached_generator = cache_value(c, text, "CMAKE_GENERATOR");
  const char *cached =
      esp_resolve(&c->e, esp_expand_user(&c->e, home), c->base);
  const char *requested = esp_resolve(&c->e, source, c->base);
  if (cc_same_path(cached, requested) &&
      (cached_generator == NULL || strcmp(cached_generator, generator) == 0))
    return;
  jb_json_tree_t *reset = cc_object(c);
  cc_set_text(c, reset, "cachedSource", cached);
  cc_set_text(c, reset, "requestedSource", requested);
  cc_set_text(c, reset, "cachedGenerator",
              cached_generator != NULL ? cached_generator : "?");
  jb_json_tree_t *allowed = cc_bool(c, managed_allowed(c, build));
  cc_set(c, reset, "allowed", cc_root(allowed));
  jb_json_tree_destroy(allowed);
  cc_set(c, plan, "resetCache", cc_root(reset));
  jb_json_tree_destroy(reset);
}

/* configured_cmake_cache_keys, sorted. */
static void configured_keys(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                            jb_vector_t *keys) {
  const jb_json_tree_view_t cache = cc_get(cc_get(config, "cmake"), "cache");
  if (!cc_is(cc_get(config, "cmake"), JB_JSON_OBJECT) ||
      !cc_is(cache, JB_JSON_OBJECT))
    return;
  for (size_t i = 0; i < jh_json_count(cache); ++i) {
    const jb_json_tree_view_t item = jh_json_item(cache, i);
    if (!cc_is(item, JB_JSON_NULL))
      (void)esp_push_unique(&c->e, keys, jh_json_key(item));
  }
  jh_list_sort(keys);
}

/* recorded_cmake_cache_keys: the record, or every transient key. */
static void recorded_keys(jh_compile_ctx_t *c, const char *build,
                          jb_vector_t *keys) {
  const char *path = esp_join(&c->e, build, CACHE_KEYS_FILE);
  jh_json_location_t where;
  jh_json_location_init(&where);
  jh_failure_t error;
  jh_failure_init(&error);
  jb_json_tree_t *tree = NULL;
  bool recorded = false;
  if (cc_ok(c) && esp_is_file(path) &&
      jh_json_read_value(&where, path, 16u * 1024u * 1024u, &tree, &error) ==
          HAL_OK) {
    const jb_json_tree_view_t root = cc_root(tree);
    if (cc_is(root, JB_JSON_ARRAY)) {
      recorded = true;
      for (size_t i = 0; i < jh_json_count(root); ++i)
        (void)esp_push_unique(&c->e, keys,
                              jh_span(cc_pystr(c, jh_json_item(root, i))));
    }
  } else if (error.status == HAL_ENOMEM) {
    (void)cc_storage(c, error.status);
  }
  for (size_t i = 0;
       !recorded && i < sizeof transient_keys / sizeof *transient_keys; ++i)
    (void)esp_push(&c->e, keys, jh_span(transient_keys[i]));
  jb_json_tree_destroy(tree);
  jh_failure_destroy(&error);
  jh_json_location_destroy(&where);
}

/* normalize_cmake_cli_value: a Windows drive or UNC value uses /. */
static const char *cli_value(jh_compile_ctx_t *c, const char *value) {
  bool windows = value[0] == '\\' && value[1] == '\\';
  for (const char *p = value; !windows && *p; ++p) {
    const bool start = p == value || p[-1] == ';';
    if (start && (jh_ascii_is_upper(p[0]) || jh_ascii_is_lower(p[0])) &&
        p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
      windows = true;
  }
  if (!windows)
    return value;
  char *copy = cc_mutable(c, value);
  for (char *p = copy; p != NULL && *p; ++p)
    if (*p == '\\')
      *p = '/';
  return copy != NULL ? copy : "";
}

static bool as_bool(jh_compile_ctx_t *c, jb_json_tree_view_t value) {
  if (cc_is(value, JB_JSON_STRING)) {
    jb_json_span_t text = {0};
    (void)jh_json_string(value, &text);
    (void)jh_python_strip(text, &text);
    char *lowered = cc_mutable(c, CC_TEXT(c, text));
    for (char *p = lowered; p != NULL && *p; ++p)
      if (jh_ascii_is_upper(*p))
        *p = (char)(*p - 'A' + 'a');
    return lowered != NULL &&
           (strcmp(lowered, "1") == 0 || strcmp(lowered, "true") == 0 ||
            strcmp(lowered, "yes") == 0 || strcmp(lowered, "on") == 0);
  }
  return cc_truthy(value);
}

/* cmake_cache_args. */
static void cache_args(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                       jb_json_tree_t *argv) {
  const char *root = cc_str(c, cc_get(config, "root"));
  if (root != NULL && root[0] != '\0')
    cc_append_text(
        c, argv,
        CC_TEXT(c, jh_span("-DJH_ROOT="),
                jh_span(cli_value(c, esp_resolve(&c->e,
                                                 esp_expand_user(&c->e, root),
                                                 c->project_dir)))));
  const char *target = config_text(c, config, "target");
  const jb_json_tree_view_t desc = target != NULL
                                       ? cc_get(cc_root(c->registry), target)
                                       : (jb_json_tree_view_t){0};
  const jb_json_tree_view_t identity = cc_get(config, "identity");
  if (cc_is(cc_get(desc, "identity"), JB_JSON_OBJECT) &&
      cc_is(identity, JB_JSON_OBJECT) &&
      as_bool(c, cc_get(identity, "enabled"))) {
    const jb_json_tree_view_t manufacturer =
        cc_get(identity, "usbManufacturer");
    const jb_json_tree_view_t product = cc_get(identity, "usbProduct");
    if (cc_truthy(manufacturer) && cc_truthy(product)) {
      cc_append_text(c, argv,
                     CC_TEXT(c, jh_span("-DJH_USB_MANUFACTURER="),
                             jh_span(cc_pystr(c, manufacturer))));
      cc_append_text(c, argv,
                     CC_TEXT(c, jh_span("-DJH_USB_PRODUCT="),
                             jh_span(cc_pystr(c, product))));
    }
    static const char *const keys[] = {"usbVid", "usbPid"};
    static const char *const names[] = {"-DJH_USB_VID=", "-DJH_USB_PID="};
    for (size_t i = 0; i < 2 && cc_ok(c); ++i) {
      jh_usb_id_kind_t kind = JH_USB_ID_ABSENT;
      uint32_t value = 0;
      if (cc_storage(
              c, jh_usb_identifier(cc_get(identity, keys[i]), &kind, &value)) &&
          kind == JH_USB_ID_VALUE)
        cc_append_text(
            c, argv,
            CC_TEXT(c, jh_span(names[i]), jh_span(esp_decimal(&c->e, value))));
    }
  }
  const jb_json_tree_view_t cmake = cc_get(config, "cmake");
  const jb_json_tree_view_t cache = cc_get(cmake, "cache");
  if (cc_is(cmake, JB_JSON_OBJECT) && cc_is(cache, JB_JSON_OBJECT))
    for (size_t i = 0; i < jh_json_count(cache) && cc_ok(c); ++i) {
      const jb_json_tree_view_t item = jh_json_item(cache, i);
      if (cc_is(item, JB_JSON_NULL))
        continue;
      const char *value = NULL;
      if (cc_is(item, JB_JSON_BOOL)) {
        bool flag = false;
        (void)jb_json_tree_get_bool(item, &flag);
        value = flag ? "ON" : "OFF";
      } else {
        value = cli_value(c, cc_pystr(c, item));
      }
      cc_append_text(c, argv,
                     CC_TEXT(c, jh_span("-D"), jh_json_key(item), jh_span("="),
                             jh_span(value)));
    }
  const jb_json_tree_view_t openocd =
      cc_get(cc_get(config, "upload"), "openocd");
  if (cc_is(cc_get(config, "upload"), JB_JSON_OBJECT) &&
      cc_is(openocd, JB_JSON_OBJECT)) {
    static const char *const semantic[] = {"bin", "interface", "target"};
    static const char *const keys[] = {"OPENOCD_BIN", "OPENOCD_INTERFACE",
                                       "OPENOCD_TARGET"};
    for (size_t i = 0; i < 3 && cc_ok(c); ++i) {
      const jb_json_tree_view_t value = cc_get(openocd, semantic[i]);
      if (cc_truthy(value))
        cc_append_text(c, argv,
                       CC_TEXT(c, jh_span("-D"), jh_span(keys[i]), jh_span("="),
                               jh_span(cli_value(c, cc_pystr(c, value)))));
    }
  }
}

/* The interpreter of Python3_EXECUTABLE: --python, JH_COMPONENT_PYTHON, then
 * python3 (POSIX) or python (Windows) from PATH; Path.resolve() of it. */
static const char *python_executable(jh_compile_ctx_t *c) {
  const char *name = c->options->python;
  if (name[0] == '\0') {
    const char *component = environment_value(c, "JH_COMPONENT_PYTHON");
    name = component != NULL && component[0] != '\0' ? component :
#ifdef _WIN32
                                                     "python";
#else
                                                     "python3";
#endif
  }
  jb_string_t found = {0};
  const char *result = "";
  if (cc_storage(c, jb_string_init(&found, jb_heap_allocator()))) {
    if (jh_process_executable(name, c->base, false, &found, c->e.failure) ==
            HAL_OK &&
        esp_is_file(jb_string_data(&found)))
      result = esp_resolve(&c->e, jb_string_data(&found), c->base);
    else if (c->e.failure->status != HAL_ENOMEM) {
      jh_failure_destroy(c->e.failure);
      jh_failure_init(c->e.failure);
      (void)CC_FAIL(c, jh_span("cannot find the Python interpreter "),
                    esp_repr(&c->e, jh_span(name)),
                    jh_span(" for Python3_EXECUTABLE; give --python or set "
                            "JH_COMPONENT_PYTHON"));
    }
  }
  jb_string_destroy(&found);
  return result;
}

/* platform_cmake_cache_args. */
static void platform_args(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                          const char *generator, jb_json_tree_t *argv) {
  cc_append_text(c, argv, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON");
  cc_append_text(c, argv,
                 CC_TEXT(c, jh_span("-DPython3_EXECUTABLE="),
                         jh_span(python_executable(c))));
  static const char *const launchers[] = {"CMAKE_C_COMPILER_LAUNCHER",
                                          "CMAKE_CXX_COMPILER_LAUNCHER"};
  for (size_t i = 0; i < 2 && cc_ok(c); ++i) {
    const char *value = environment_value(c, launchers[i]);
    if (value != NULL)
      cc_append_text(c, argv,
                     CC_TEXT(c, jh_span("-D"), jh_span(launchers[i]),
                             jh_span("="), jh_span(cli_value(c, value))));
  }
  const char *target = config_text(c, config, "target");
  const char *ninja = host_tool(c, "ninja");
  const char *picotool = environment_value(c, "JH_PICOTOOL_EXECUTABLE");
  if (picotool == NULL || picotool[0] == '\0')
    picotool = host_tool(c, "picotool");
  if (picotool == NULL && native_rp(target))
    picotool = esp_join(
        &c->e,
        esp_join(&c->e,
                 esp_join(&c->e, esp_join(&c->e, c->root, ".build"), "tools"),
                 "picotool"),
#ifdef _WIN32
        "picotool.exe"
#else
        "picotool"
#endif
    );
  const char *riscv = host_tool(c, "riscv");
  const char *openocd = host_tool(c, "openocd");
  bool ninja_generator = strlen(generator) == 5;
  for (size_t i = 0; ninja_generator && i < 5; ++i)
    ninja_generator = (generator[i] | 0x20) == "ninja"[i];
  if (ninja != NULL && ninja_generator)
    cc_append_text(
        c, argv, CC_TEXT(c, jh_span("-DCMAKE_MAKE_PROGRAM="), jh_span(ninja)));
  if (picotool != NULL && native_rp(target))
    cc_append_text(
        c, argv,
        CC_TEXT(c, jh_span("-DJH_PICOTOOL_EXECUTABLE="), jh_span(picotool)));
  if (riscv != NULL && target != NULL && strcmp(target, "rp2350-riscv") == 0)
    cc_append_text(
        c, argv,
        CC_TEXT(c, jh_span("-DPICO_TOOLCHAIN_PATH="),
                jh_span(cc_purepath(
                    c, esp_parent(&c->e, esp_parent(&c->e, riscv))))));
  if (openocd != NULL && target != NULL && strcmp(target, "stm32g474") == 0)
    cc_append_text(c, argv,
                   CC_TEXT(c, jh_span("-DOPENOCD_BIN="), jh_span(openocd)));
}

/* cmake_process_environment and the JH_EXECUTABLE pair of the configure. */
static void environment(jh_compile_ctx_t *c, jb_json_tree_t *plan,
                        bool configure) {
  jb_json_tree_t *env = cc_object(c);
  if (has_host_tools(c)) {
    jb_vector_t directories = {0};
    const jb_json_tree_view_t tools = cc_get(cc_root(c->host), "tools");
    if (esp_list_init(&c->e, &directories)) {
      for (size_t i = 0; i < jh_json_count(tools); ++i) {
        jb_json_span_t text = {0};
        if (jh_json_string(jh_json_item(tools, i), &text) && text.length != 0)
          (void)esp_push_unique(
              &c->e, &directories,
              jh_span(cc_purepath(c, esp_parent(&c->e, CC_TEXT(c, text)))));
      }
      const char *path = environment_value(c, "PATH");
#ifdef _WIN32
      const char *separator = ";";
#else
      const char *separator = ":";
#endif
      (void)esp_push(&c->e, &directories, jh_span(path != NULL ? path : ""));
      cc_set_text(c, env, "PATH",
                  esp_join_list(&c->e, &directories, separator));
    }
    jh_list_destroy(&directories);
    const char *managed = managed_root(c);
    if (managed != NULL)
      cc_set_text(c, env, "JH_MANAGED_BUILD_ROOT",
                  cc_purepath(c, esp_expand_user(&c->e, managed)));
  }
  if (configure) {
    cc_set_text(c, env, "JH_EXECUTABLE", c->options->jh);
    cc_set_text(c, env, "JH_EXECUTABLE_ROOT", c->root);
  }
  cc_set(c, plan, configure ? "configureEnvironment" : "buildEnvironment",
         cc_root(env));
  jb_json_tree_destroy(env);
}

static void set_list(jh_compile_ctx_t *c, jb_json_tree_t *plan, const char *key,
                     const jb_vector_t *list) {
  jb_json_tree_t *array = cc_array(c);
  for (size_t i = 0; i < jb_vector_size(list) && cc_ok(c); ++i)
    cc_append_text(c, array, esp_at(list, i));
  cc_set(c, plan, key, cc_root(array));
  jb_json_tree_destroy(array);
}

static void set_text_member(jh_compile_ctx_t *c, jb_json_tree_t *plan,
                            const char *key, const char *text) {
  cc_set_text(c, plan, key, text);
}

/* The CMake build: command_build with cmake_build_config. */
static bool cmake_plan(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                       jb_json_tree_t *plan) {
  const bool debug = c->options->debug;
  const jb_json_tree_view_t cmake = cc_get(config, "cmake");
  const bool has_cmake = cc_is(cmake, JB_JSON_OBJECT);
  const char *program = NULL;
  if (has_cmake && cc_truthy(cc_get(cmake, "executable")))
    program = cc_pystr(c, cc_get(cmake, "executable"));
  if (program == NULL)
    program = host_tool(c, "cmake");
  if (program == NULL)
    program = "cmake";
  const char *generator = has_cmake && cc_truthy(cc_get(cmake, "generator"))
                              ? cc_pystr(c, cc_get(cmake, "generator"))
                              : "Ninja";
  const char *source = source_dir(c, config);
  const char *build = cmake_build_dir(c, config, debug);
  const char *artifacts = artifact_dir(c, config);
  /* cmake_build_config: the debug tree builds CMAKE_BUILD_TYPE=Debug. */
  jb_json_tree_t *effective = cc_copy(c, config);
  if (debug) {
    jb_json_tree_t *section = cc_section(c, cc_root(effective), "cmake");
    jb_json_tree_t *cache = cc_section(c, cc_root(section), "cache");
    cc_set_text(c, cache, "CMAKE_BUILD_TYPE", "Debug");
    cc_set(c, section, "cache", cc_root(cache));
    cc_set(c, effective, "cmake", cc_root(section));
    jb_json_tree_destroy(cache);
    jb_json_tree_destroy(section);
  }
  const jb_json_tree_view_t view = cc_root(effective);
  jb_vector_t current = {0}, recorded = {0}, removed = {0};
  jb_json_tree_t *configure = cc_array(c), *build_argv = cc_array(c);
  if (!esp_list_init(&c->e, &current) || !esp_list_init(&c->e, &recorded) ||
      !esp_list_init(&c->e, &removed))
    goto done;
  set_text_member(c, plan, "sourceDir", source);
  set_text_member(c, plan, "cmakeBuildDir", build);
  set_text_member(c, plan, "artifactDir", artifacts);
  set_text_member(c, plan, "lockFile",
                  esp_join(&c->e, artifacts, ".jh-build.lock"));
  reset_decision(c, plan, source, build, generator);
  configured_keys(c, view, &current);
  recorded_keys(c, build, &recorded);
  jh_list_sort(&recorded);
  for (size_t i = 0; i < jb_vector_size(&recorded) && cc_ok(c); ++i)
    if (!jh_list_contains(&current, jh_string_span(jh_list_at(&recorded, i))))
      (void)esp_push(&c->e, &removed, jh_string_span(jh_list_at(&recorded, i)));
  cc_append_text(c, configure, program);
  cc_append_text(c, configure, "-S");
  cc_append_text(c, configure, source);
  cc_append_text(c, configure, "-B");
  cc_append_text(c, configure, build);
  cc_append_text(c, configure, "-G");
  cc_append_text(c, configure, generator);
  for (size_t i = 0; i < jb_vector_size(&removed) && cc_ok(c); ++i)
    cc_append_text(
        c, configure,
        CC_TEXT(c, jh_span("-U"), jh_string_span(jh_list_at(&removed, i))));
  cache_args(c, view, configure);
  platform_args(c, view, generator, configure);
  cc_set(c, plan, "configure", cc_root(configure));
  environment(c, plan, true);
  set_list(c, plan, "cacheKeys", &current);
  /* cmake_targets: the defaults, then the manifest's targets. */
  const char *target_name = debug ? "firmware_debug" : "firmware";
  const jb_json_tree_view_t targets = cc_get(cmake, "targets");
  if (has_cmake && cc_is(targets, JB_JSON_OBJECT) &&
      cc_has(targets, debug ? "buildDebug" : "build"))
    target_name = cc_pystr(c, cc_get(targets, debug ? "buildDebug" : "build"));
  cc_append_text(c, build_argv, program);
  cc_append_text(c, build_argv, "--build");
  cc_append_text(c, build_argv, build);
  cc_append_text(c, build_argv, "--target");
  cc_append_text(c, build_argv, target_name);
  cc_set(c, plan, "build", cc_root(build_argv));
  environment(c, plan, false);
  /* synchronize_cmake_firmware_artifacts: the published files. */
  jb_vector_t published = {0};
  if (esp_list_init(&c->e, &published)) {
    static const char *const always[] = {"firmware.elf", "firmware.bin",
                                         "firmware.hex", "firmware.map"};
    for (size_t i = 0; i < 4; ++i)
      (void)esp_push(&c->e, &published, jh_span(always[i]));
    const char *target = config_text(c, config, "target");
    if (native_rp(target)) {
      (void)esp_push(&c->e, &published, jh_span("firmware.uf2"));
      if (jh_list_contains(&c->resolved_features, jh_span("HAL_ENABLE_OTA")))
        (void)esp_push(&c->e, &published, jh_span("firmware.ota"));
    }
    set_list(c, plan, "artifacts", &published);
  }
  jh_list_destroy(&published);
done:
  jb_json_tree_destroy(build_argv);
  jb_json_tree_destroy(configure);
  jh_list_destroy(&removed);
  jh_list_destroy(&recorded);
  jh_list_destroy(&current);
  jb_json_tree_destroy(effective);
  return cc_ok(c);
}

/* The ESP-IDF build: esp_idf_runner_command of the build and artifacts. */
static bool esp_plan(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                     jb_json_tree_t *plan) {
  if (c->options->debug)
    return CC_FAIL(c, jh_span("ESP-IDF debug builds are not implemented by "
                              "jh-vscode"));
  const jb_json_tree_view_t esp = cc_get(config, "espIdf");
  if (!cc_is(esp, JB_JSON_OBJECT))
    return CC_FAIL(c, jh_span("ESP-IDF target is missing its tooling "
                              "configuration"));
  const char *builder = cc_str(c, cc_get(esp, "builder"));
  if (builder == NULL || strcmp(builder, "jh") != 0)
    return CC_FAIL(c, jh_span("ESP-IDF target names an unsupported builder: "),
                   esp_repr_tree(&c->e, cc_get(esp, "builder")));
  const char *target = config_text(c, config, "target");
  const char *board = config_text(c, config, "board");
  if (target == NULL || board == NULL)
    return CC_FAIL(c, jh_span("ESP-IDF runner requires a resolved target and "
                              "board"));
  const char *output = artifact_dir(c, config);
  if (!managed_allowed(c, output))
    return CC_FAIL(c,
                   jh_span("refusing ESP-IDF output outside managed "
                           "artifact roots: "),
                   jh_span(output));
  const jb_json_tree_view_t cache = cc_get(cc_get(config, "cmake"), "cache");
  static const char *const unsupported[] = {"JH_EXTRA_SOURCES",
                                            "JH_EXTRA_LIBRARIES"};
  for (size_t i = 0; i < 2; ++i) {
    const jb_json_tree_view_t value = cc_get(cache, unsupported[i]);
    jb_json_span_t text = {0};
    if (cc_truthy(value) &&
        jh_python_strip(jh_span(cc_pystr(c, value)), &text) && text.length)
      return CC_FAIL(c, jh_span(unsupported[i]),
                     jh_span(" is not supported by ESP-IDF builds; keep the "
                             "sources in the project directory or its src/"));
  }
  static const char *const actions[] = {"build", "artifacts"};
  for (size_t a = 0; a < 2 && cc_ok(c); ++a) {
    jb_json_tree_t *argv = cc_array(c);
    cc_append_text(c, argv, c->options->jh);
    cc_append_text(c, argv, "esp");
    cc_append_text(c, argv, actions[a]);
    cc_append_text(c, argv, "--root");
    cc_append_text(c, argv, c->root);
    cc_append_text(c, argv, "--project");
    cc_append_text(c, argv, c->project_dir);
    cc_append_text(c, argv, "--target");
    cc_append_text(c, argv, target);
    const char *assembly = config_text(c, config, "assembly");
    cc_append_text(c, argv, assembly != NULL ? "--assembly" : "--board");
    cc_append_text(c, argv, assembly != NULL ? assembly : board);
    cc_append_text(c, argv, "--output");
    cc_append_text(c, argv, output);
    const char *variant = config_text(c, config, "variant");
    if (variant != NULL) {
      cc_append_text(c, argv, "--variant");
      cc_append_text(c, argv, variant);
    }
    static const char *const keys[] = {"JH_EXTRA_INCLUDES",
                                       "JH_LINK_LIBRARIES"};
    static const char *const options[] = {"--include-dir", "--link-library"};
    for (size_t k = 0; k < 2; ++k) {
      const jb_json_tree_view_t value = cc_get(cache, keys[k]);
      const char *text = cc_truthy(value) ? cc_pystr(c, value) : "";
      while (cc_ok(c) && *text) {
        const char *end = strchr(text, ';');
        const size_t length = end != NULL ? (size_t)(end - text) : strlen(text);
        jb_json_span_t item = {text, length};
        if (jh_python_strip(item, &item) && item.length != 0) {
          cc_append_text(c, argv, options[k]);
          cc_append_text(c, argv, CC_TEXT(c, item));
        }
        text += length + (end != NULL ? 1 : 0);
      }
    }
    cc_set(c, plan, actions[a], cc_root(argv));
    jb_json_tree_destroy(argv);
  }
  cc_set_text(c, plan, "artifactDir", output);
  const char *manifest = cc_str(c, cc_get(esp, "artifactManifest"));
  if (manifest == NULL || manifest[0] == '\0')
    return CC_FAIL(c, jh_span("ESP-IDF target is missing its artifact "
                              "manifest path"));
  cc_set_text(
      c, plan, "artifactManifest",
      esp_resolve(&c->e, esp_expand_user(&c->e, manifest), c->project_dir));
  return cc_ok(c);
}

bool cc_plan(jh_compile_ctx_t *c, jb_json_tree_t *config,
             jb_json_tree_t **out) {
  if (!load_host(c))
    return false;
  jb_json_tree_t *plan = cc_object(c);
  const jb_json_tree_view_t root = cc_root(config);
  const char *toolchain = cc_str(c, cc_get(root, "toolchain"));
  jb_json_tree_t *debug = cc_bool(c, c->options->debug);
  cc_set(c, plan, "debug", cc_root(debug));
  jb_json_tree_destroy(debug);
  cc_set(c, plan, "toolchain", cc_get(root, "toolchain"));
  if (toolchain != NULL && strcmp(toolchain, "cmake") == 0)
    (void)cmake_plan(c, root, plan);
  else if (toolchain != NULL && strcmp(toolchain, "esp-idf") == 0)
    (void)esp_plan(c, root, plan);
  else
    (void)CC_FAIL(c, jh_span("build for toolchain '"),
                  jh_span(cc_pystr(c, cc_get(root, "toolchain"))),
                  jh_span("' is not implemented"));
  if (!cc_ok(c)) {
    jb_json_tree_destroy(plan);
    return false;
  }
  *out = plan;
  return true;
}

/* ---- Entry ---- */

void jh_compile_init(jh_compile_t *compile) {
  if (compile != NULL)
    *compile = (jh_compile_t){NULL, NULL};
}

void jh_compile_destroy(jh_compile_t *compile) {
  if (compile == NULL)
    return;
  jb_json_tree_destroy(compile->plan);
  jb_json_tree_destroy(compile->config);
  *compile = (jh_compile_t){NULL, NULL};
}

hal_status_t jh_compile_plan(const jh_compile_options_t *options,
                             jh_compile_t *compile, jh_failure_t *failure) {
  if (options == NULL || compile == NULL || failure == NULL ||
      options->root == NULL || options->base == NULL ||
      options->project == NULL || options->target == NULL ||
      options->board == NULL || options->assembly == NULL ||
      options->variant == NULL || options->port == NULL ||
      options->bootsel_volume == NULL || options->python == NULL ||
      options->jh == NULL || compile->config != NULL || compile->plan != NULL)
    return jh_fail_arguments(failure);
  jh_compile_ctx_t c = {0};
  static const jb_vector_t empty_list = {0};
  c.esp_options.root = options->root;
  c.esp_options.base = options->base;
  c.esp_options.sources = &empty_list;
  c.e.options = &c.esp_options;
  c.e.failure = failure;
  c.e.log = stderr;
  c.options = options;
  jh_board_registry_init(&c.e.boards);
  jh_feature_registry_init(&c.e.features);
  jh_hardware_registry_init(&c.e.hardware);
  jb_json_tree_t *config = NULL, *plan = NULL;
  if (!esp_list_init(&c.e, &c.e.strings) ||
      !esp_list_init(&c.e, &c.resolved_features))
    goto done;
  c.root =
      esp_resolve(&c.e, esp_expand_user(&c.e, options->root), options->base);
  c.base = options->base;
  if (options->project[0] == '\0') {
    (void)jh_fail_text(failure, HAL_EINVAL, 2, "compile requires --project");
    goto done;
  }
  c.project_dir =
      esp_resolve(&c.e, esp_expand_user(&c.e, options->project), options->base);
  if (cc_ok(&c) && !esp_is_dir(c.project_dir)) {
    (void)CC_FAIL(&c, jh_span("project directory does not exist: "),
                  jh_span(c.project_dir));
    goto done;
  }
  if (cc_model(&c, &config) && cc_plan(&c, config, &plan)) {
    compile->config = config;
    compile->plan = plan;
    config = plan = NULL;
  }
done:
  jb_json_tree_destroy(plan);
  jb_json_tree_destroy(config);
  jb_json_tree_destroy(c.host);
  jb_json_tree_destroy(c.sources);
  jb_json_tree_destroy(c.registry);
  jh_list_destroy(&c.resolved_features);
  jh_list_destroy(&c.e.strings);
  /* Also a registry whose load failed part way. */
  jh_hardware_registry_destroy(&c.e.hardware);
  jh_feature_registry_destroy(&c.e.features);
  jh_board_registry_destroy(&c.e.boards);
  return failure->status;
}

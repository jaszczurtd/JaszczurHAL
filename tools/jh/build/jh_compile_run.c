/* The configure step of jh compile: configure_cmake_project of
 * vscode/runtime/jh_vscode.py over a computed plan (jh_compile.h). C11. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "jh_build_support.h"
#include "jh_compile_internal.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include "jh_process_support.h"
#include "jh_project_build.h"
#include <jb/array.h>
#include <jb/filesystem.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define jh_isatty _isatty
#define jh_fileno _fileno
#else
#include <unistd.h>
#define jh_isatty isatty
#define jh_fileno fileno
#endif

/* EXIT_BUILD of jh-vscode: command_build reports every failed step so. */
#define BUILD_FAILED 5
#define CACHE_KEYS_FILE ".jh-vscode-cache-keys.json"

static bool step_fail(jh_compile_ctx_t *c, const jb_json_span_t *parts,
                      size_t count) {
  if (cc_ok(c))
    (void)jh_fail(c->e.failure, HAL_EIO, BUILD_FAILED, parts, count);
  return false;
}
#define STEP_FAIL(c, ...) step_fail((c), JH_PARTS(__VA_ARGS__))

static const char *plan_text(jh_compile_ctx_t *c, jb_json_tree_view_t object,
                             const char *key) {
  const char *text = cc_str(c, cc_get(object, key));
  return text != NULL ? text : "";
}

static bool plan_flag(jb_json_tree_view_t object, const char *key) {
  bool flag = false;
  const jb_json_tree_view_t value = cc_get(object, key);
  return cc_is(value, JB_JSON_BOOL) &&
         jb_json_tree_get_bool(value, &flag) == HAL_OK && flag;
}

/* color_enabled: FORCE_COLOR, else not NO_COLOR and stdout is a terminal. */
static bool color_enabled(jh_compile_ctx_t *c) {
  const char *force = cc_environment(c, "FORCE_COLOR");
  if (force != NULL && force[0] != '\0')
    return true;
  const char *none = cc_environment(c, "NO_COLOR");
  if (none != NULL && none[0] != '\0')
    return false;
  return jh_isatty(jh_fileno(stdout)) != 0;
}

/* reset_stale_cmake_cache_if_needed over the plan's decision. */
static bool reset_cache(jh_compile_ctx_t *c, jb_json_tree_view_t plan,
                        const char *build) {
  const jb_json_tree_view_t reset = cc_get(plan, "resetCache");
  if (!cc_is(reset, JB_JSON_OBJECT))
    return cc_ok(c);
  const char *cached = plan_text(c, reset, "cachedSource");
  const char *requested = plan_text(c, reset, "requestedSource");
  if (!plan_flag(reset, "allowed"))
    return STEP_FAIL(c,
                     jh_span("refusing to reset stale CMake cache outside "
                             "managed artifact roots: "),
                     jh_span(build), jh_span("\n       cached source: "),
                     jh_span(cached), jh_span("\n       requested source: "),
                     jh_span(requested));
  const char *warning =
      CC_TEXT(c,
              jh_span("warning: stale CMake cache contract changed; "
                      "resetting "),
              jh_span(build), jh_span(" (source "), jh_span(cached),
              jh_span(" -> "), jh_span(requested), jh_span(", generator "),
              jh_span(plan_text(c, reset, "cachedGenerator")), jh_span(" -> "),
              jh_span(plan_text(c, reset, "generator")), jh_span(")"));
  if (!cc_ok(c))
    return false;
  const bool color = color_enabled(c);
  (void)fprintf(stderr, "%s%s%s\n", color ? "\033[33m" : "", warning,
                color ? "\033[0m" : "");
  (void)fflush(stderr);
  const char *cache = esp_join(&c->e, build, "CMakeCache.txt");
  const hal_status_t status = jb_fs_remove(cache, false);
  if (status != HAL_OK)
    return status == HAL_ENOMEM
               ? cc_storage(c, status)
               : STEP_FAIL(c, jh_span("failed to reset stale CMake cache in "),
                           jh_span(build), jh_span(": cannot remove "),
                           jh_span(cache), jh_span(": "),
                           jh_span(hal_status_to_string(status)));
  /* shutil.rmtree(..., ignore_errors=True) */
  (void)jb_fs_remove(esp_join(&c->e, build, "CMakeFiles"), true);
  return cc_ok(c);
}

/* The process environment: the current one with the plan's entries. */
static bool child_environment(jh_compile_ctx_t *c, jb_json_tree_view_t plan,
                              jb_vector_t *environment) {
  if (!esp_environment_capture(&c->e, environment))
    return false;
  const jb_json_tree_view_t added = cc_get(plan, "configureEnvironment");
  for (size_t i = 0;
       cc_is(added, JB_JSON_OBJECT) && i < jh_json_count(added) && cc_ok(c);
       ++i) {
    const jb_json_tree_view_t item = jh_json_item(added, i);
    const char *value = cc_str(c, item);
    (void)esp_environment_set(&c->e, environment, CC_TEXT(c, jh_json_key(item)),
                              value != NULL ? value : "");
  }
  return cc_ok(c);
}

/* The program subprocess starts, links kept: a path with a separator from
 * the working directory, a bare name from PATH (the child's PATH on POSIX, as
 * Popen with env does; the PATH of this process on Windows, as CreateProcess
 * does). NULL when it does not exist. */
static const char *find_program(jh_compile_ctx_t *c,
                                const jb_vector_t *environment,
                                const char *name) {
  if (name[0] == '\0')
    return NULL;
#ifdef _WIN32
  (void)environment;
#else
  if (strchr(name, '/') == NULL)
    return esp_find_program(&c->e, environment, name);
#endif
  {
    jb_string_t found = {0};
    const char *result = NULL;
    jh_failure_t lookup;
    jh_failure_init(&lookup);
    if (cc_storage(c, jb_string_init(&found, jb_heap_allocator()))) {
      const hal_status_t status =
          jh_process_executable(name, c->base, false, &found, &lookup);
      if (status == HAL_OK && esp_is_file(jb_string_data(&found)))
        result = CC_TEXT(c, jh_string_span(&found));
      else if (status == HAL_ENOMEM)
        (void)cc_storage(c, status);
    }
    jh_failure_destroy(&lookup);
    jb_string_destroy(&found);
    return result;
  }
}

/* Path(cmd[0]).name.lower() in {"cmake", "cmake.exe"} of run_command. */
static bool named_cmake(const char *program) {
  const char *name = esp_name(program);
  static const char *const names[] = {"cmake", "cmake.exe"};
  for (size_t n = 0; n < COUNTOF(names); ++n) {
    size_t i = 0;
    while (name[i] != '\0' && names[n][i] != '\0' &&
           (name[i] | (jh_ascii_is_upper(name[i]) ? 0x20 : 0)) == names[n][i])
      ++i;
    if (name[i] == '\0' && names[n][i] == '\0')
      return true;
  }
  return false;
}

hal_status_t jh_compile_command_line(jb_json_tree_view_t argv,
                                     jb_string_t *out) {
  if (out == NULL)
    return HAL_EINVAL;
  hal_status_t status = HAL_OK;
  jh_put(out, &status, "+");
  for (size_t i = 0; i < jh_json_count(argv); ++i) {
    jb_json_span_t text = {0};
    (void)jh_json_string(jh_json_item(argv, i), &text);
    jh_put(out, &status, " ");
    jh_put_span(out, &status, text);
  }
  jh_put(out, &status, "\n");
  return status;
}

/* "+ <argv>" of run_command with verbose, on the log stream. */
static bool echo(jh_compile_ctx_t *c, jb_json_tree_view_t argv) {
  jb_string_t line = {0};
  hal_status_t status = jb_string_init(&line, jb_heap_allocator());
  if (status == HAL_OK)
    status = jh_compile_command_line(argv, &line);
  if (cc_storage(c, status) &&
      (fwrite(jb_string_data(&line), 1, jb_string_size(&line), c->e.log) !=
           jb_string_size(&line) ||
       fflush(c->e.log) != 0))
    (void)jh_fail_text(c->e.failure, HAL_EIO, 3, "cannot write the log");
  jb_string_destroy(&line);
  return cc_ok(c);
}

/* Start program with argv[1:] in the working directory and environment
 * ("NAME=VALUE" strings), as run_command: a program named cmake has
 * stderr=STDOUT on the log stream, any other keeps stderr. A failure to start
 * or a nonzero exit fails the step. */
static bool start(jh_compile_ctx_t *c, const char *program, const char *name,
                  jb_json_tree_view_t argv, const jb_vector_t *environment) {
  jb_vector_t arguments = {0};
  if (!esp_list_init(&c->e, &arguments))
    return false;
  for (size_t i = 1; i < jh_json_count(argv) && cc_ok(c); ++i) {
    const char *item = cc_str(c, jh_json_item(argv, i));
    (void)esp_push(&c->e, &arguments, jh_span(item != NULL ? item : ""));
  }
  int code = -1;
  hal_status_t status = HAL_OK;
  if (cc_ok(c)) {
    const esp_process_t process = {program,
                                   &arguments,
                                   NULL,
                                   environment,
                                   named_cmake(name) ? ESP_RUN_MERGE
                                                     : ESP_RUN_FORWARD,
                                   true,
                                   false,
                                   0,
                                   NULL,
                                   NULL};
    status = esp_process(&c->e, &process, &code);
  }
  jh_list_destroy(&arguments);
  if (!cc_ok(c))
    return false;
  if (status == HAL_ENOENT)
    return STEP_FAIL(c, jh_span("command not found: "), jh_span(name));
  if (status != HAL_OK)
    return STEP_FAIL(c, jh_span("failed to run "), jh_span(name), jh_span(": "),
                     jh_span(hal_status_to_string(status)));
  if (code != 0)
    return STEP_FAIL(c, jh_span("CMake configure failed with exit code "),
                     jh_span(esp_decimal(&c->e, (uint64_t)code)));
  return true;
}

static bool run_configure(jh_compile_ctx_t *c, jb_json_tree_view_t plan) {
  const jb_json_tree_view_t argv = cc_get(plan, "configure");
  if (!cc_is(argv, JB_JSON_ARRAY) || jh_json_count(argv) == 0)
    return STEP_FAIL(c, jh_span("the plan has no configure command"));
  const char *name = cc_str(c, jh_json_item(argv, 0));
  if (name == NULL)
    name = "";
  jb_vector_t environment = {0};
  if (child_environment(c, plan, &environment) &&
      (!plan_flag(plan, "verbose") || echo(c, argv))) {
    const char *program = find_program(c, &environment, name);
    if (program != NULL)
      (void)start(c, program, name, argv, &environment);
    else if (cc_ok(c))
      (void)STEP_FAIL(c, jh_span("command not found: "), jh_span(name));
  }
  jh_list_destroy(&environment);
  return cc_ok(c);
}

/* record_cmake_cache_keys: json.dumps(sorted keys, indent=2) and a newline,
 * written in text mode. */
static bool record_keys(jh_compile_ctx_t *c, jb_json_tree_view_t plan,
                        const char *build) {
  const jb_json_tree_view_t keys = cc_get(plan, "cacheKeys");
  if (!cc_is(keys, JB_JSON_ARRAY))
    return STEP_FAIL(c, jh_span("the plan has no cache keys"));
  const jb_json_tree_format_t format = {SIZE_MAX / 2, 2, false, true};
  jb_string_t text = {0};
  size_t written = 0;
  hal_status_t status = jb_string_init(&text, jb_heap_allocator());
  if (status == HAL_OK)
    status =
        jb_json_tree_format(keys, &format, jh_string_sink, &text, &written);
  if (status == HAL_OK)
    status = jh_append(&text, "\n");
#ifdef _WIN32
  jb_string_t translated = {0};
  if (status == HAL_OK)
    status = jb_string_init(&translated, jb_heap_allocator());
  for (size_t i = 0; i < jb_string_size(&text) && status == HAL_OK; ++i)
    status = jb_string_data(&text)[i] == '\n'
                 ? jb_string_append(&translated, "\r\n", 2)
                 : jb_string_append(&translated, jb_string_data(&text) + i, 1);
  if (status == HAL_OK) {
    jb_string_t swap = text;
    text = translated;
    translated = swap;
  }
  jb_string_destroy(&translated);
#endif
  if (cc_storage(c, status)) {
    const char *path = esp_join(&c->e, build, CACHE_KEYS_FILE);
    jh_failure_t error;
    jh_failure_init(&error);
    bool changed = false;
    if (cc_ok(c) && jh_project_build_write(path, jh_string_span(&text),
                                           &changed, &error) != HAL_OK) {
      if (error.status == HAL_ENOMEM)
        (void)cc_storage(c, error.status);
      else
        (void)STEP_FAIL(
            c, jh_span("failed to record managed CMake cache keys in "),
            jh_span(build), jh_span(": "), jh_string_span(&error.message));
    }
    jh_failure_destroy(&error);
  }
  jb_string_destroy(&text);
  return cc_ok(c);
}

hal_status_t jh_compile_configure(const jh_compile_options_t *options,
                                  const jh_compile_t *compile,
                                  jh_failure_t *failure) {
  if (options == NULL || compile == NULL || failure == NULL ||
      options->root == NULL || options->base == NULL || compile->plan == NULL)
    return jh_fail_arguments(failure);
  jh_compile_ctx_t c = {0};
  static const jb_vector_t empty_list = {0};
  c.esp_options.root = options->root;
  c.esp_options.base = options->base;
  c.esp_options.sources = &empty_list;
  c.esp_options.canceled = options->canceled;
  c.esp_options.cancel_context = options->cancel_context;
  c.e.options = &c.esp_options;
  c.e.failure = failure;
  /* The log stream of the echo and of the child's stdout (esp_process). */
  c.e.log = options->log != NULL ? options->log : stdout;
  c.options = options;
  c.base = options->base;
  jh_board_registry_init(&c.e.boards);
  jh_feature_registry_init(&c.e.features);
  jh_hardware_registry_init(&c.e.hardware);
  const jb_json_tree_view_t plan = cc_root(compile->plan);
  if (esp_list_init(&c.e, &c.e.strings)) {
    const char *toolchain = cc_str(&c, cc_get(plan, "toolchain"));
    const char *build = plan_text(&c, plan, "cmakeBuildDir");
    if (toolchain == NULL || strcmp(toolchain, "cmake") != 0)
      (void)jh_fail_text(failure, HAL_EUNSUPPORTED, 2,
                         "--configure-only applies to CMake projects; an "
                         "ESP-IDF project configures in jh esp build");
    else if (reset_cache(&c, plan, build) && run_configure(&c, plan))
      (void)record_keys(&c, plan, build);
  }
  jh_list_destroy(&c.e.strings);
  jh_hardware_registry_destroy(&c.e.hardware);
  jh_feature_registry_destroy(&c.e.features);
  jh_board_registry_destroy(&c.e.boards);
  return failure->status;
}

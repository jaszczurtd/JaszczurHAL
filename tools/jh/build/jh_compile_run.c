/* The steps of jh compile over a computed plan (jh_compile.h): the build lock,
 * then configure_cmake_project and the rest of command_build of
 * vscode/runtime/jh_vscode.py (artifact invalidation, preflight, cmake
 * --build with its diagnostics, publication and the memory map overview).
 * C11. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "jh_build_support.h"
#include "jh_compile_internal.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include "jh_process_support.h"
#include "jh_project_build.h"
#include "jh_python_repr.h"
#include <inttypes.h>
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
/* run_command_capture keeps the last 250000 characters of a build's output in
 * whole lines for its diagnostics; here bytes. */
#define CAPTURE_LIMIT 250000u

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

/* The process environment: the current one with the entries of the plan
 * member key. */
static bool child_environment(jh_compile_ctx_t *c, jb_json_tree_view_t plan,
                              const char *key, jb_vector_t *environment) {
  if (!esp_environment_capture(&c->e, environment))
    return false;
  const jb_json_tree_view_t added = cc_get(plan, key);
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
 * ("NAME=VALUE" strings). Without capture as run_command: a program named
 * cmake has stderr=STDOUT on the log stream, any other keeps stderr. With
 * capture as run_command_capture: both streams reach the log stream and
 * capture. A failure to start fails the step; *code is the exit code. */
static bool start(jh_compile_ctx_t *c, const char *program, const char *name,
                  jb_json_tree_view_t argv, const jb_vector_t *environment,
                  jb_string_t *capture, int *code) {
  jb_vector_t arguments = {0};
  if (!esp_list_init(&c->e, &arguments))
    return false;
  for (size_t i = 1; i < jh_json_count(argv) && cc_ok(c); ++i) {
    const char *item = cc_str(c, jh_json_item(argv, i));
    (void)esp_push(&c->e, &arguments, jh_span(item != NULL ? item : ""));
  }
  hal_status_t status = HAL_OK;
  if (cc_ok(c)) {
    const int mode =
        capture != NULL || named_cmake(name) ? ESP_RUN_MERGE : ESP_RUN_FORWARD;
    const esp_process_t process = {
        program, &arguments, NULL,    environment, mode,         true,
        false,   0,          capture, NULL,        CAPTURE_LIMIT};
    status = esp_process(&c->e, &process, code);
  }
  jh_list_destroy(&arguments);
  if (!cc_ok(c))
    return false;
  if (status == HAL_ENOENT)
    return STEP_FAIL(c, jh_span("command not found: "), jh_span(name));
  if (status != HAL_OK)
    return STEP_FAIL(c, jh_span("failed to run "), jh_span(name), jh_span(": "),
                     jh_span(hal_status_to_string(status)));
  return true;
}

/* One CMake step of the plan: the command of member key with the environment
 * of member environment_key, echoed first when the plan is verbose. *code is
 * its exit code; a missing program or a failed start fails the step. */
static bool run_step(jh_compile_ctx_t *c, jb_json_tree_view_t plan,
                     const char *key, const char *environment_key,
                     jb_string_t *capture, int *code) {
  const jb_json_tree_view_t argv = cc_get(plan, key);
  if (!cc_is(argv, JB_JSON_ARRAY) || jh_json_count(argv) == 0)
    return STEP_FAIL(c, jh_span("the plan has no "), jh_span(key),
                     jh_span(" command"));
  const char *name = cc_str(c, jh_json_item(argv, 0));
  if (name == NULL)
    name = "";
  jb_vector_t environment = {0};
  *code = -1;
  if (child_environment(c, plan, environment_key, &environment) &&
      (!plan_flag(plan, "verbose") || echo(c, argv))) {
    const char *program = find_program(c, &environment, name);
    if (program != NULL)
      (void)start(c, program, name, argv, &environment, capture, code);
    else if (cc_ok(c))
      (void)STEP_FAIL(c, jh_span("command not found: "), jh_span(name));
  }
  jh_list_destroy(&environment);
  return cc_ok(c);
}

static bool step_code(jh_compile_ctx_t *c, const char *what, int code) {
  return code == 0 || STEP_FAIL(c, jh_span("CMake "), jh_span(what),
                                jh_span(" failed with exit code "),
                                jh_span(esp_decimal(&c->e, (uint64_t)code)));
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

/* ---- The build lock and the stable artifacts ---- */

/* The uploadable files of the artifact directory (STABLE_FIRMWARE_ARTIFACTS).
 */
static const char *const stable_artifacts[] = {"firmware.elf", "firmware.bin",
                                               "firmware.hex", "firmware.map",
                                               "firmware.uf2", "firmware.ota"};

/* .jh-build.lock of the artifact directory, created with its parents. A
 * lock another build holds is announced on stderr, then awaited without a
 * time limit (cancellation still stops the wait); *waited tells. */
static bool lock_build(jh_compile_ctx_t *c, jb_json_tree_view_t plan,
                       jb_file_lock_t **lock, bool *waited) {
  const char *artifacts = plan_text(c, plan, "artifactDir");
  const char *path = plan_text(c, plan, "lockFile");
  if (!cc_ok(c))
    return false;
  if (artifacts[0] == '\0' || path[0] == '\0')
    return STEP_FAIL(c, jh_span("the plan has no build lock"));
  hal_status_t status = jb_fs_mkdir(artifacts, true);
  if (status != HAL_OK)
    return status == HAL_ENOMEM
               ? cc_storage(c, status)
               : STEP_FAIL(c, jh_span("cannot create "), jh_span(artifacts),
                           jh_span(": "),
                           jh_span(hal_status_to_string(status)));
  const jh_compile_options_t *o = c->options;
  status = jb_file_lock_acquire(path, 0, o->canceled, o->cancel_context, lock);
  if (status == HAL_EBUSY) {
    *waited = true;
    (void)fprintf(stderr, "waiting for another build of %s (%s)\n", artifacts,
                  path);
    (void)fflush(stderr);
    status = jb_file_lock_acquire(path, UINT32_MAX, o->canceled,
                                  o->cancel_context, lock);
  }
  if (status == HAL_ECANCELED) {
    (void)jh_fail_text(c->e.failure, status, 130, "canceled");
    return false;
  }
  if (status != HAL_OK)
    return status == HAL_ENOMEM
               ? cc_storage(c, status)
               : STEP_FAIL(c, jh_span("cannot lock "), jh_span(path),
                           jh_span(": "),
                           jh_span(hal_status_to_string(status)));
  return true;
}

/* A run that waited for the lock plans again: the other build may have reset
 * the cache or recorded other cache keys meanwhile, and a stale plan would
 * reset its fresh tree again. The new plan must keep the same lock. A run that
 * locks at once keeps its plan, which reads the CMake tree in its last
 * milliseconds; another configure and build cannot fit between that read and
 * the lock, and planning twice would cost every build about half a second. */
static bool replan(jh_compile_ctx_t *c, jh_compile_t *compile,
                   jb_json_tree_view_t *config, jb_json_tree_view_t *plan) {
  const char *locked = plan_text(c, *plan, "lockFile");
  jh_compile_t fresh;
  jh_compile_init(&fresh);
  if (cc_ok(c) && jh_compile_plan(c->options, &fresh, c->e.failure) == HAL_OK) {
    jh_compile_destroy(compile);
    *compile = fresh;
    *config = cc_root(compile->config);
    *plan = cc_root(compile->plan);
    if (strcmp(plan_text(c, *plan, "lockFile"), locked) != 0)
      (void)STEP_FAIL(c,
                      jh_span("the build directory changed while waiting "
                              "for the build lock "),
                      jh_span(locked));
  } else {
    jh_compile_destroy(&fresh);
  }
  return cc_ok(c);
}

/* invalidate_stable_firmware_artifacts: no stale upload survives a build that
 * fails. */
static bool invalidate(jh_compile_ctx_t *c, jb_json_tree_view_t plan) {
  const char *artifacts = plan_text(c, plan, "artifactDir");
  for (size_t i = 0; i < COUNTOF(stable_artifacts) && cc_ok(c); ++i) {
    const char *path = esp_join(&c->e, artifacts, stable_artifacts[i]);
    if (!cc_ok(c) || !esp_is_file(path))
      continue;
    const hal_status_t status = jb_fs_remove(path, false);
    if (status != HAL_OK)
      return status == HAL_ENOMEM
                 ? cc_storage(c, status)
                 : STEP_FAIL(c,
                             jh_span("failed to invalidate firmware artifacts: "
                                     "cannot remove "),
                             jh_span(path), jh_span(": "),
                             jh_span(hal_status_to_string(status)));
  }
  return cc_ok(c);
}

/* ---- Diagnostics ---- */

/* print_build_diagnostics: a blank line, the yellow title and the messages
 * (jb_string_t) on stderr. */
static void print_diagnostics(jh_compile_ctx_t *c,
                              const jb_vector_t *messages) {
  if (jb_vector_size(messages) == 0)
    return;
  const bool color = color_enabled(c);
  (void)fprintf(stderr, "\n%sJaszczurHAL diagnostics:%s\n",
                color ? "\033[33m" : "", color ? "\033[0m" : "");
  for (size_t i = 0; i < jb_vector_size(messages); ++i)
    (void)fprintf(stderr, "  - %s\n", esp_at(messages, i));
  (void)fflush(stderr);
}

/* A name in matching quotes of the linker, [`'][^`']+[`'], at text[at]: its
 * end, or 0 without one. */
static size_t quoted(jb_json_span_t text, size_t at, jb_json_span_t *name) {
  if (at >= text.length || (text.data[at] != '`' && text.data[at] != '\''))
    return 0;
  size_t end = at + 1;
  while (end < text.length && text.data[end] != '`' && text.data[end] != '\'')
    ++end;
  if (end == at + 1 || end == text.length)
    return 0;
  *name = (jb_json_span_t){text.data + at + 1, end - at - 1};
  return end + 1;
}

/* One match of the linker message at text[at]: region `R' overflowed by N
 * bytes (overflow) or section `S' will not fit in region `R'. Returns its
 * end, or 0. */
static size_t linker_match(jb_json_span_t text, size_t at, bool overflow,
                           jb_json_span_t *section, jb_json_span_t *region,
                           uint64_t *bytes) {
  size_t i = at + strlen(overflow ? "region " : "section ");
  i = quoted(text, i, overflow ? region : section);
  if (i == 0)
    return 0;
  if (!overflow) {
    if (!cc_literal_at(text, i, " will not fit in region "))
      return 0;
    return quoted(text, i + strlen(" will not fit in region "), region);
  }
  if (!cc_literal_at(text, i, " overflowed by "))
    return 0;
  i += strlen(" overflowed by ");
  const size_t digits = i;
  uint64_t value = 0;
  while (i < text.length && text.data[i] >= '0' && text.data[i] <= '9') {
    const uint64_t digit = (uint64_t)(text.data[i] - '0');
    value = value > (UINT64_MAX - digit) / 10 ? UINT64_MAX : value * 10 + digit;
    ++i;
  }
  if (i == digits || !cc_literal_at(text, i, " bytes"))
    return 0;
  *bytes = value;
  return i + strlen(" bytes");
}

/* diagnose_build_output over the captured output: each overflowed region
 * once, then each region a section will not fit, unless it overflowed. */
static void diagnose(jh_compile_ctx_t *c, jb_json_span_t output,
                     const char *display, jb_vector_t *messages) {
  jb_vector_t overflowed = {0}, sections = {0};
  if (!esp_list_init(&c->e, &overflowed) || !esp_list_init(&c->e, &sections))
    goto done;
  for (int pass = 0; pass < 2 && cc_ok(c); ++pass) {
    const bool overflow = pass == 0;
    const char *prefix = overflow ? "region " : "section ";
    for (size_t at = 0; at < output.length && cc_ok(c);) {
      if (!cc_literal_at(output, at, prefix)) {
        ++at;
        continue;
      }
      jb_json_span_t section = {0}, region = {0};
      uint64_t bytes = 0;
      const size_t end =
          linker_match(output, at, overflow, &section, &region, &bytes);
      if (end == 0) {
        ++at;
        continue;
      }
      at = end;
      const char *name = CC_TEXT(c, region);
      jb_vector_t *seen = overflow ? &overflowed : &sections;
      if (!cc_ok(c) || jh_list_contains(seen, jh_span(name)) ||
          (!overflow && jh_list_contains(&overflowed, jh_span(name))))
        continue;
      (void)esp_push(&c->e, seen, jh_span(name));
      (void)esp_push(
          &c->e, messages,
          overflow
              ? jh_span(CC_TEXT(
                    c, jh_span("axis-3: firmware does not fit "),
                    jh_span(display), jh_span(" region "), region,
                    jh_span("; linker reports overflow by "),
                    jh_span(cc_format_size(c, bytes)), jh_span(" ("),
                    jh_span(esp_decimal(&c->e, bytes)), jh_span(" bytes).")))
              : jh_span(CC_TEXT(c, jh_span("axis-3: section "), section,
                                jh_span(" will not fit "), jh_span(display),
                                jh_span(" region "), region, jh_span("."))));
    }
  }
done:
  jh_list_destroy(&sections);
  jh_list_destroy(&overflowed);
}

/* The preflight messages of the plan stop the build before CMake runs. */
static bool check_preflight(jh_compile_ctx_t *c, jb_json_tree_view_t plan) {
  const jb_json_tree_view_t found = cc_get(plan, "preflight");
  jb_vector_t messages = {0};
  if (esp_list_init(&c->e, &messages)) {
    for (size_t i = 0; i < jh_json_count(found) && cc_ok(c); ++i) {
      const char *message = cc_str(c, jh_json_item(found, i));
      if (message != NULL)
        (void)esp_push(&c->e, &messages, jh_span(message));
    }
    if (cc_ok(c) && jb_vector_size(&messages) != 0) {
      print_diagnostics(c, &messages);
      (void)STEP_FAIL(c, jh_span("the build preflight stopped the build"));
    }
  }
  jh_list_destroy(&messages);
  return cc_ok(c);
}

/* cmake --build of the plan; on a failure the diagnostics of the linker
 * messages in the end of its output. */
static bool run_build(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                      jb_json_tree_view_t plan) {
  jb_string_t output = {0};
  jb_vector_t messages = {0};
  int code = -1;
  if (!cc_storage(c, jb_string_init(&output, jb_heap_allocator())) ||
      !esp_list_init(&c->e, &messages))
    goto done;
  if (run_step(c, plan, "build", "buildEnvironment", &output, &code) &&
      code != 0) {
    const jb_json_span_t text = jh_string_span(&output);
    const size_t start = esp_tail(text, CAPTURE_LIMIT);
    diagnose(c, (jb_json_span_t){text.data + start, text.length - start},
             cc_display_name(c, config), &messages);
    if (cc_ok(c))
      print_diagnostics(c, &messages);
    (void)step_code(c, "build", code);
  }
done:
  jh_list_destroy(&messages);
  jb_string_destroy(&output);
  return cc_ok(c);
}

/* ---- Publication ---- */

/* The size limit of one published artifact. */
#define ARTIFACT_LIMIT (1024u * 1024u * 1024u)

static bool sync_fail(jh_compile_ctx_t *c, const char *what, const char *path,
                      hal_status_t status) {
  return status == HAL_ENOMEM
             ? cc_storage(c, status)
             : STEP_FAIL(c,
                         jh_span("failed to synchronize firmware artifacts: "),
                         jh_span(what), jh_span(path), jh_span(": "),
                         jh_span(hal_status_to_string(status)));
}

/* shutil.copy2 of the content: the destination gets the bytes of source,
 * published atomically with the permissions a new file gets. */
static bool copy_artifact(jh_compile_ctx_t *c, const char *source,
                          const char *destination) {
  jb_file_data_t data = {0};
  const hal_status_t status = jb_file_read(source, ARTIFACT_LIMIT, &data);
  if (status != HAL_OK) {
    jb_file_release(&data);
    return sync_fail(c, "cannot read ", source, status);
  }
  jh_failure_t error;
  jh_failure_init(&error);
  bool changed = false;
  if (jh_project_build_write(
          destination, (jb_json_span_t){(const char *)data.data, data.length},
          &changed, &error) != HAL_OK)
    (void)sync_fail(c, "cannot write ", destination, error.status);
  jh_failure_destroy(&error);
  jb_file_release(&data);
  return cc_ok(c);
}

/* synchronize_cmake_firmware_artifacts: every published file of the plan
 * comes from the CMake tree (firmware.map also as firmware.elf.map), also
 * after a build that changed nothing; a UF2 or OTA image the plan does not
 * publish leaves the artifact directory. */
static bool publish(jh_compile_ctx_t *c, jb_json_tree_view_t config,
                    jb_json_tree_view_t plan) {
  const char *tree = plan_text(c, plan, "cmakeBuildDir");
  const char *artifacts = plan_text(c, plan, "artifactDir");
  const jb_json_tree_view_t published = cc_get(plan, "artifacts");
  const jb_json_tree_view_t target = cc_get(config, "target");
  jb_vector_t sources = {0}, names = {0};
  if (!esp_list_init(&c->e, &sources) || !esp_list_init(&c->e, &names))
    goto done;
  for (size_t i = 0; i < jh_json_count(published) && cc_ok(c); ++i) {
    const char *name = cc_str(c, jh_json_item(published, i));
    if (name == NULL)
      continue;
    const char *candidates[] = {
        name, strcmp(name, "firmware.map") == 0 ? "firmware.elf.map" : NULL};
    const char *source = NULL;
    for (size_t k = 0; k < COUNTOF(candidates) && source == NULL; ++k) {
      const char *path =
          candidates[k] != NULL ? esp_join(&c->e, tree, candidates[k]) : NULL;
      if (path != NULL && esp_is_file(path))
        source = path;
    }
    if (source == NULL) {
      (void)STEP_FAIL(c, jh_span("successful "),
                      jh_span(cc_truthy(target) ? cc_pystr(c, target) : ""),
                      jh_span(" build omitted "), jh_span(name),
                      jh_span(" in "), jh_span(tree));
      goto done;
    }
    (void)esp_push(&c->e, &sources, jh_span(source));
    (void)esp_push(&c->e, &names, jh_span(name));
  }
  for (size_t i = 0; i < jb_vector_size(&sources) && cc_ok(c); ++i) {
    const char *source = esp_at(&sources, i);
    const char *destination = esp_join(&c->e, artifacts, esp_at(&names, i));
    if (cc_ok(c) && !cc_same_path(esp_resolve(&c->e, source, c->base),
                                  esp_resolve(&c->e, destination, c->base)))
      (void)copy_artifact(c, source, destination);
  }
  static const char *const optional[] = {"firmware.uf2", "firmware.ota"};
  for (size_t i = 0; i < COUNTOF(optional) && cc_ok(c); ++i) {
    const char *stale = esp_join(&c->e, artifacts, optional[i]);
    if (!jh_list_contains(&names, jh_span(optional[i])) && cc_ok(c) &&
        esp_is_file(stale)) {
      const hal_status_t status = jb_fs_remove(stale, false);
      if (status != HAL_OK)
        (void)sync_fail(c, "cannot remove ", stale, status);
    }
  }
done:
  jh_list_destroy(&names);
  jh_list_destroy(&sources);
  return cc_ok(c);
}

/* ---- Entry ---- */

/* The steps of one run under the build lock: configure only, or the whole
 * build of command_build. */
static hal_status_t run(const jh_compile_options_t *options,
                        jh_compile_t *compile, jh_failure_t *failure,
                        bool build) {
  if (options == NULL || compile == NULL || failure == NULL ||
      options->root == NULL || options->base == NULL ||
      compile->config == NULL || compile->plan == NULL)
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
  jb_json_tree_view_t config = cc_root(compile->config);
  jb_json_tree_view_t plan = cc_root(compile->plan);
  jb_file_lock_t *lock = NULL;
  bool waited = false;
  int code = -1;
  if (esp_list_init(&c.e, &c.e.strings)) {
    const char *toolchain = cc_str(&c, cc_get(plan, "toolchain"));
    if (toolchain == NULL || strcmp(toolchain, "cmake") != 0)
      (void)jh_fail_text(
          failure, HAL_EUNSUPPORTED, 2,
          build ? "jh compile builds CMake projects; an ESP-IDF project "
                  "builds with jh esp build"
                : "--configure-only applies to CMake projects; an ESP-IDF "
                  "project configures in jh esp build");
    else if (lock_build(&c, plan, &lock, &waited) &&
             (!waited || replan(&c, compile, &config, &plan))) {
      const char *tree = plan_text(&c, plan, "cmakeBuildDir");
      if ((!build || (invalidate(&c, plan) && check_preflight(&c, plan))) &&
          reset_cache(&c, plan, tree) &&
          run_step(&c, plan, "configure", "configureEnvironment", NULL,
                   &code) &&
          step_code(&c, "configure", code) && record_keys(&c, plan, tree) &&
          build && run_build(&c, config, plan) && publish(&c, config, plan))
        (void)cc_memory_overview(&c, tree, cc_str(&c, cc_get(plan, "objdump")));
    }
  }
  const hal_status_t released = jb_file_lock_release(&lock);
  if (released != HAL_OK && failure->status == HAL_OK)
    (void)STEP_FAIL(&c, jh_span("cannot release the build lock: "),
                    jh_span(hal_status_to_string(released)));
  jh_list_destroy(&c.e.strings);
  jh_hardware_registry_destroy(&c.e.hardware);
  jh_feature_registry_destroy(&c.e.features);
  jh_board_registry_destroy(&c.e.boards);
  return failure->status;
}

hal_status_t jh_compile_configure(const jh_compile_options_t *options,
                                  jh_compile_t *compile,
                                  jh_failure_t *failure) {
  return run(options, compile, failure, false);
}

hal_status_t jh_compile_build(const jh_compile_options_t *options,
                              jh_compile_t *compile, jh_failure_t *failure) {
  return run(options, compile, failure, true);
}

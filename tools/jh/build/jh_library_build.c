/* sched_getaffinity and CPU_COUNT of the CPUs this process may use. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "jh_library_build.h"
#include "jh_build_support.h"
#include "jh_esp_build.h"
#include "jh_json_digest.h"
#include "jh_json_file.h"
#include "jh_process_support.h"
#include <jb/filesystem.h>
#include <jb/process.h>
#include <jb/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#ifdef __linux__
#include <sched.h>
#endif
#endif

/* The executable suffix of the host. */
#ifdef _WIN32
#define HOST_EXE ".exe"
#else
#define HOST_EXE ""
#endif

/* One build in progress: options, the strings it owns and the first
 * failure. Every helper stops once a failure is recorded. */
typedef struct {
  const jh_library_build_options_t *options;
  jh_failure_t *failure;
  jb_vector_t strings; /* jb_string_t texts the build owns. */
  const char *cmake;   /* cmake of PATH, found before the first use. */
  FILE *log;           /* Progress and child stdout. */
} build_t;

static bool ok(build_t *b) { return b->failure->status == HAL_OK; }

static bool storage(build_t *b, hal_status_t status) {
  if (status != HAL_OK && ok(b))
    (void)jh_fail_storage(b->failure, status);
  return ok(b);
}

/* die of the runners: exit code 1 with the message. */
static bool die(build_t *b, const jb_json_span_t *parts, size_t count) {
  if (ok(b))
    (void)jh_fail(b->failure, HAL_ECONFIG, 1, parts, count);
  return false;
}

static const char *text(build_t *b, const jb_json_span_t *parts, size_t count) {
  if (!ok(b) || !storage(b, jh_list_push_parts(&b->strings, parts, count)))
    return "";
  return jb_string_data(
      jh_list_at(&b->strings, jb_vector_size(&b->strings) - 1));
}

static FILE *progress = NULL;

static void say(const char *color, const char *label, const char *message) {
  FILE *file = progress != NULL ? progress : stdout;
  (void)fprintf(file, "\033[%sm[%s]\033[0m %s\n", color, label, message);
  (void)fflush(file);
}
static void info(const char *message) { say("0;36", "INFO", message); }
static void success(const char *message) { say("0;32", "OK", message); }
static void warn(const char *message) { say("1;33", "WARN", message); }

static bool absolute(const char *path) {
  bool result = false;
  return jb_fs_path_is_absolute(path, &result) == HAL_OK && result;
}

/* "${REPO_ROOT}/${path}" of the runners for a relative path; the text is
 * not normalized. */
static const char *rooted(build_t *b, const char *path) {
  if (path[0] == '\0' || absolute(path))
    return path;
  return text(b,
              JH_PARTS(jh_span(b->options->root), jh_span("/"), jh_span(path)));
}

/* realpath -m: Path.resolve() semantics of the longest existing prefix. */
static const char *canonical(build_t *b, const char *path) {
  jb_string_t out = {0};
  if (!storage(b, jb_string_init(&out, jb_heap_allocator())))
    return "";
  const char *result = "";
  if (jh_python_resolve(path, b->options->base, &out, b->failure) == HAL_OK)
    result = text(b, JH_PARTS(jh_string_span(&out)));
  jb_string_destroy(&out);
  return result;
}

static jb_fs_kind_t kind(const char *path) {
  jb_fs_info_t entry = {0};
  return jb_fs_status(path, true, &entry) == HAL_OK ? entry.kind : 0;
}

static bool executable(const char *path) {
  if (kind(path) != JB_FS_REGULAR)
    return false;
#ifdef _WIN32
  return true;
#else
  return access(path, X_OK) == 0;
#endif
}

static const char *environment(build_t *b, const char *name,
                               const char *fallback) {
  jb_string_t value = {0};
  bool found = false;
  const char *result = fallback;
  if (storage(b, jb_string_init(&value, jb_heap_allocator())) &&
      storage(b, jh_process_environment(name, &value, &found)) && found &&
      jb_string_size(&value) != 0)
    result = text(b, JH_PARTS(jh_string_span(&value)));
  jb_string_destroy(&value);
  return result;
}

/* jh_resolve_build_output: the requested directory, or default_relative
 * below <root>/.build, which must contain the result. NULL when outside. */
static const char *build_output(build_t *b, const char *requested,
                                const char *default_relative) {
  const char *build_root = canonical(
      b, text(b, JH_PARTS(jh_span(b->options->root), jh_span("/.build"))));
  const char *resolved;
  if (requested[0] == '\0')
    resolved = text(b, JH_PARTS(jh_span(build_root), jh_span("/"),
                                jh_span(default_relative)));
  else if (absolute(requested))
    resolved = canonical(b, requested);
  else
    resolved =
        canonical(b, text(b, JH_PARTS(jh_span(b->options->root), jh_span("/"),
                                      jh_span(requested))));
  if (!ok(b))
    return NULL;
  return jh_build_inside(resolved, build_root) ? resolved : NULL;
}

static bool same_path(const char *a, const char *b) {
  return jh_build_inside(a, b) && jh_build_inside(b, a);
}

/* The unsafe-output guard of the Pico SDK runner, for every provider: a
 * .build that links elsewhere must not make the build directory, which
 * --clean removes, the file system root, the checkout or its parent. */
static bool safe_output(build_t *b, const char *output) {
  const char *root = b->options->root;
  size_t parent = strlen(root);
  while (parent > 0 && !jh_build_separator(root[parent - 1]))
    --parent;
  while (parent > 1 && jh_build_separator(root[parent - 1]) &&
         !(parent == 3 && root[1] == ':'))
    --parent;
  const char *parent_dir = text(b, JH_PARTS(((jb_json_span_t){root, parent})));
  bool file_system_root = true;
  for (const char *c = output; *c != '\0' && file_system_root; ++c)
    file_system_root = jh_build_separator(*c) || (c == output + 1 && *c == ':');
  if (!ok(b))
    return false;
  if (file_system_root || same_path(output, root) ||
      (parent != 0 && same_path(output, parent_dir)))
    return die(b, JH_PARTS(jh_span("Refusing unsafe build output directory: "),
                           jh_span(output)));
  return true;
}

static hal_status_t forward(void *context, jb_process_stream_t stream,
                            const unsigned char *data, size_t length) {
  (void)context;
  FILE *file = stream == JB_PROCESS_STDERR ? stderr
               : progress != NULL          ? progress
                                           : stdout;
  if (fwrite(data, 1, length, file) != length || fflush(file) != 0)
    return HAL_EIO;
  return HAL_OK;
}

/* jb_process_run hands its one context to both callbacks: the output sink
 * reads the capture buffer, the cancellation adapter the caller's callback
 * and context. */
typedef struct {
  const jh_library_build_options_t *options;
  jb_string_t *text;
  size_t limit;
} process_call_t;

static hal_status_t capture(void *context, jb_process_stream_t stream,
                            const unsigned char *data, size_t length) {
  process_call_t *call = context;
  if (stream != JB_PROCESS_STDOUT)
    return forward(NULL, stream, data, length);
  if (jb_string_size(call->text) + length > call->limit)
    return HAL_EOVERFLOW;
  return jb_string_append(call->text, (const char *)data, length);
}

static bool process_canceled(void *context) {
  const process_call_t *call = context;
  return call->options->canceled(call->options->cancel_context);
}

/* Run executable with arguments (a jb_string_t list) in the working
 * directory, forwarding its output, or capturing stdout into captured. A
 * nonzero exit fails with that exit code, as set -e ended the runners. */
static bool run(build_t *b, const char *program, const jb_vector_t *arguments,
                jb_string_t *captured) {
  if (!ok(b))
    return false;
  jb_vector_t pointers = {0};
  if (!storage(b, jb_vector_init(&pointers, sizeof(const char *),
                                 jb_heap_allocator())))
    return false;
  for (size_t i = 0; i < jb_vector_size(arguments) && ok(b); ++i) {
    const char *item = jb_string_data(jh_list_at(arguments, i));
    (void)storage(b, jb_vector_append(&pointers, &item, 1));
  }
  const char *terminator = NULL;
  (void)storage(b, jb_vector_append(&pointers, &terminator, 1));
  process_call_t call = {b->options, captured, 64u * 1024u * 1024u};
  jb_process_options_t options = {0};
  options.executable = program;
  options.arguments = jb_vector_const_data(&pointers);
  options.working_directory = b->options->base;
  options.output = captured != NULL ? capture : forward;
  options.context = &call;
  options.canceled = b->options->canceled != NULL ? process_canceled : NULL;
  jb_process_result_t result = {0};
  if (ok(b)) {
    const hal_status_t status = jb_process_run(&options, &result);
    if (status == HAL_ECANCELED)
      (void)jh_fail_text(b->failure, status, 130, "canceled");
    else if (status != HAL_OK)
      (void)jh_fail(b->failure, status, 3,
                    JH_PARTS(jh_span("cannot run "), jh_span(program)));
    else if (result.term_signal != 0 || result.exit_code != 0) {
      const int code = jh_build_exit_code(&result);
      char number[16];
      (void)snprintf(number, sizeof number, "%d", code);
      (void)jh_fail(b->failure, HAL_EIO, code,
                    JH_PARTS(jh_span(program),
                             jh_span(" failed with exit code "),
                             jh_span(number)));
    }
  }
  jb_vector_destroy(&pointers);
  return ok(b);
}

static bool push(build_t *b, jb_vector_t *list, const jb_json_span_t *parts,
                 size_t count) {
  return ok(b) && storage(b, jh_list_push_parts(list, parts, count));
}

/* scripts/ensure_<name>.sh with arguments; Windows runs the component
 * manager those scripts start. */
static bool ensure(build_t *b, const char *script, const char *component,
                   const jb_vector_t *arguments) {
  jb_vector_t list = {0};
  if (!storage(b, jh_list_init(&list)))
    return false;
  const char *program;
#ifdef _WIN32
  (void)script;
  program = environment(b, "JH_COMPONENT_PYTHON", "python");
  jb_string_t found = {0};
  if (storage(b, jb_string_init(&found, jb_heap_allocator())) &&
      storage(b, jh_process_executable(program, b->options->base, false, &found,
                                       b->failure)))
    program = text(b, JH_PARTS(jh_string_span(&found)));
  jb_string_destroy(&found);
  (void)push(b, &list,
             JH_PARTS(jh_span(b->options->root),
                      jh_span("/scripts/component_manager.py")));
  (void)push(b, &list, JH_PARTS(jh_span("component")));
  (void)push(b, &list, JH_PARTS(jh_span(component)));
#else
  (void)component;
  program = text(b, JH_PARTS(jh_span(b->options->root), jh_span("/scripts/"),
                             jh_span(script)));
#endif
  for (size_t i = 0; i < jb_vector_size(arguments) && ok(b); ++i)
    (void)push(b, &list, JH_PARTS(jh_string_span(jh_list_at(arguments, i))));
  const bool result = run(b, program, &list, NULL);
  jh_list_destroy(&list);
  return result;
}

static bool cmake(build_t *b, const jb_vector_t *arguments) {
  if (!ok(b))
    return false;
  if (b->cmake == NULL) {
    jb_string_t found = {0};
    if (storage(b, jb_string_init(&found, jb_heap_allocator())) &&
        storage(b, jh_process_executable("cmake", b->options->base, false,
                                         &found, b->failure)))
      b->cmake = text(b, JH_PARTS(jh_string_span(&found)));
    jb_string_destroy(&found);
    if (!ok(b))
      return false;
  }
  return run(b, b->cmake, arguments, NULL);
}

/* The validation of jh_validate_hal_defines: no generator expressions, and
 * HAL_ENABLE_* only as a bare symbol or with the value 1. */
static bool validate_defines(build_t *b) {
  const jb_vector_t *defines = b->options->defines;
  for (size_t i = 0; i < jb_vector_size(defines) && ok(b); ++i) {
    const char *definition = jb_string_data(jh_list_at(defines, i));
    const char *normalized =
        strncmp(definition, "-D", 2) == 0 ? definition + 2 : definition;
    if (strstr(normalized, "$<") != NULL)
      return die(b, JH_PARTS(jh_span("[JH-CFG-VALUE] "), jh_span(definition),
                             jh_span(" is unsupported; generator expressions "
                                     "are not accepted")));
    if (strstr(normalized, "HAL_ENABLE_") == NULL)
      continue;
    bool valid = strncmp(normalized, "HAL_ENABLE_", 11) == 0;
    const char *c = normalized + 11;
    const char *start = c;
    while (valid &&
           ((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_'))
      ++c;
    valid = valid && c != start && (*c == '\0' || strcmp(c, "=1") == 0);
    if (!valid)
      return die(b, JH_PARTS(jh_span("[JH-CFG-VALUE] "), jh_span(definition),
                             jh_span(" is unsupported; use a standalone bare "
                                     "symbol or an explicit value of 1")));
  }
  return ok(b);
}

/* jh_device_tree_library: 0 when config_dir has device_tree.json, 1 for the
 * board path; a contradicting selector fails with exit code 2. */
static int device_tree(build_t *b, const char *config_dir) {
  const jh_library_build_options_t *o = b->options;
  if (config_dir[0] != '\0' &&
      kind(text(b, JH_PARTS(jh_span(config_dir),
                            jh_span("/device_tree.json")))) == JB_FS_REGULAR) {
    if (o->board[0] != '\0') {
      (void)jh_fail(b->failure, HAL_ECONFIG, 2,
                    JH_PARTS(jh_span("[JH-HW-BOARD] "), jh_span(config_dir),
                             jh_span("/device_tree.json selects the board "
                                     "through its assemblies; use --assembly "
                                     "instead of --board")));
      return -1;
    }
    return 0;
  }
  if (o->assembly[0] != '\0' || o->variant[0] != '\0') {
    (void)jh_fail_text(b->failure, HAL_ECONFIG, 2,
                       "[JH-HW-ASSEMBLY] --assembly and --variant need -p DIR "
                       "with device_tree.json");
    return -1;
  }
  return 1;
}

/* The requests of the device tree key: variant, all-features, the FreeRTOS
 * request when the provider has one, then the -D values as given. */
static const char *library_key(build_t *b, const char *config_dir,
                               bool with_freertos) {
  const jh_library_build_options_t *o = b->options;
  jb_vector_t requests = {0};
  jb_string_t key = {0};
  const char *result = "";
  if (storage(b, jh_list_init(&requests)) &&
      storage(b, jb_string_init(&key, jb_heap_allocator()))) {
    (void)push(b, &requests,
               JH_PARTS(jh_span("variant="), jh_span(o->variant)));
    (void)push(b, &requests,
               JH_PARTS(jh_span(o->all_features ? "all=1" : "all=0")));
    if (with_freertos)
      (void)push(b, &requests,
                 JH_PARTS(jh_span(o->freertos ? "freertos=1" : "freertos=0")));
    for (size_t i = 0; i < jb_vector_size(o->defines) && ok(b); ++i)
      (void)push(b, &requests,
                 JH_PARTS(jh_string_span(jh_list_at(o->defines, i))));
    if (ok(b) && jh_library_key(config_dir, o->base, o->assembly, &requests,
                                &key, b->failure) == HAL_OK)
      result = text(b, JH_PARTS(jh_string_span(&key)));
  }
  jb_string_destroy(&key);
  jh_list_destroy(&requests);
  return result;
}

static const char *selection(build_t *b, bool tree, const char *board) {
  if (!tree)
    return text(b, JH_PARTS(jh_span("board "), jh_span(board)));
  const char *assembly = b->options->assembly;
  return text(b,
              JH_PARTS(jh_span("assembly "),
                       jh_span(assembly[0] != '\0' ? assembly
                                                   : "chosen by the target")));
}

/* cp -f: an existing destination keeps its mode and a new one gets 0644, or
 * the file gets exactly mode when it is not zero (cp -f, then chmod). */
static bool copy_mode(build_t *b, const char *from, const char *to,
                      uint32_t mode) {
  jb_file_data_t data = {0};
  const hal_status_t status = jb_file_read(from, 1024u * 1024u * 1024u, &data);
  if (status != HAL_OK) {
    jb_file_release(&data);
    return storage(b, status);
  }
  bool changed = false;
  const hal_status_t written =
      mode == 0 && kind(to) == JB_FS_REGULAR
          ? jb_file_write_atomic(to, data.data, data.length, false, &changed)
          : jb_file_write_atomic_mode(to, data.data, data.length, false,
                                      mode != 0 ? mode : 0644, &changed);
  jb_file_release(&data);
  if (written != HAL_OK)
    (void)jh_fail(b->failure, written, 1,
                  JH_PARTS(jh_span("cannot write "), jh_span(to)));
  return ok(b);
}

static bool copy_file(build_t *b, const char *from, const char *to) {
  return copy_mode(b, from, to, 0);
}

/* jh_collect_generated_headers: the board path copies its headers from the
 * board directory into include/generated; a device tree build must already
 * have them there with jh_hardware.h. */
static bool collect_headers(build_t *b, const char *output, const char *board,
                            bool tree) {
  const char *include =
      text(b, JH_PARTS(jh_span(output), jh_span("/include/generated")));
  if (ok(b) && jb_fs_mkdir(include, true) != HAL_OK)
    return die(b, JH_PARTS(jh_span("cannot create "), jh_span(include)));
  static const char *const tree_headers[] = {
      "jh_board_config.h", "jh_link_contract.h", "jh_hardware.h"};
  if (tree) {
    for (size_t i = 0; i < COUNTOF(tree_headers) && ok(b); ++i) {
      const char *path = text(b, JH_PARTS(jh_span(include), jh_span("/"),
                                          jh_span(tree_headers[i])));
      if (kind(path) != JB_FS_REGULAR)
        return die(b, JH_PARTS(jh_span("Generated header not found: "),
                               jh_span(path)));
    }
    return ok(b);
  }
  for (size_t i = 0; i < 2 && ok(b); ++i) {
    const char *source = text(
        b, JH_PARTS(jh_span(output), jh_span("/generated/boards/"),
                    jh_span(b->options->target), jh_span("/"), jh_span(board),
                    jh_span("/"), jh_span(tree_headers[i])));
    if (kind(source) != JB_FS_REGULAR)
      return die(b, JH_PARTS(jh_span("Generated board header not found: "),
                             jh_span(source)));
    (void)copy_file(b, source,
                    text(b, JH_PARTS(jh_span(include), jh_span("/"),
                                     jh_span(tree_headers[i]))));
  }
  return ok(b);
}

static const char *processor_count(build_t *b) {
  long count = 0;
#ifdef _WIN32
  count = (long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#elif defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof set, &set) == 0)
    count = CPU_COUNT(&set);
  if (count <= 0)
    count = sysconf(_SC_NPROCESSORS_ONLN);
#else
  count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
  char number[32];
  (void)snprintf(number, sizeof number, "%ld", count > 0 ? count : 4L);
  return text(b, JH_PARTS(jh_span(number)));
}

static const char *joined_defines(build_t *b, const jb_vector_t *defines) {
  jb_string_t out = {0};
  hal_status_t status = jb_string_init(&out, jb_heap_allocator());
  for (size_t i = 0; i < jb_vector_size(defines) && status == HAL_OK; ++i) {
    if (i != 0)
      jh_put(&out, &status, ";");
    jh_put_span(&out, &status, jh_string_span(jh_list_at(defines, i)));
  }
  const char *result =
      storage(b, status) ? text(b, JH_PARTS(jh_string_span(&out))) : "";
  jb_string_destroy(&out);
  return result;
}

static bool clean_output(build_t *b, const char *output) {
  if (!b->options->clean || kind(output) != JB_FS_DIRECTORY || !ok(b))
    return ok(b);
  info(text(b, JH_PARTS(jh_span("Cleaning "), jh_span(output))));
  const hal_status_t status = jb_fs_remove(output, true);
  if (status != HAL_OK)
    (void)jh_fail(b->failure, status, 1,
                  JH_PARTS(jh_span("cannot remove "), jh_span(output)));
  return ok(b);
}

/* -S <root>/link_libraries/<family> -B <output>: the CMake project of a
 * library family and its build directory. */
static void push_project(build_t *b, jb_vector_t *args, const char *family,
                         const char *output) {
  (void)push(b, args, JH_PARTS(jh_span("-S")));
  (void)push(b, args,
             JH_PARTS(jh_span(b->options->root), jh_span("/link_libraries/"),
                      jh_span(family)));
  (void)push(b, args, JH_PARTS(jh_span("-B")));
  (void)push(b, args, JH_PARTS(jh_span(output)));
}

/* The target, board, assembly and variant cache entries of both families. */
static void push_selection(build_t *b, jb_vector_t *args, const char *board) {
  const jh_library_build_options_t *o = b->options;
  (void)push(b, args, JH_PARTS(jh_span("-DJH_TARGET="), jh_span(o->target)));
  (void)push(b, args, JH_PARTS(jh_span("-DJH_BOARD="), jh_span(board)));
  (void)push(b, args,
             JH_PARTS(jh_span("-DJH_ASSEMBLY="), jh_span(o->assembly)));
  (void)push(b, args, JH_PARTS(jh_span("-DJH_VARIANT="), jh_span(o->variant)));
}

/* "Building with <jobs> parallel jobs..." and cmake --build of output: only
 * the JaszczurHAL target when library_target is set, with the jobs flag of
 * the family (--parallel or -j). */
static bool build_step(build_t *b, const char *output, bool library_target,
                       const char *jobs_flag) {
  const char *jobs =
      b->options->jobs != NULL ? b->options->jobs : processor_count(b);
  info(text(b, JH_PARTS(jh_span("Building with "), jh_span(jobs),
                        jh_span(" parallel jobs..."))));
  jb_vector_t args = {0};
  bool built = false;
  if (storage(b, jh_list_init(&args))) {
    (void)push(b, &args, JH_PARTS(jh_span("--build")));
    (void)push(b, &args, JH_PARTS(jh_span(output)));
    if (library_target) {
      (void)push(b, &args, JH_PARTS(jh_span("--target")));
      (void)push(b, &args, JH_PARTS(jh_span("JaszczurHAL")));
    }
    (void)push(b, &args, JH_PARTS(jh_span(jobs_flag)));
    (void)push(b, &args, JH_PARTS(jh_span(jobs)));
    built = cmake(b, &args);
  }
  jh_list_destroy(&args);
  return built;
}

/* nm --defined-only of elf has symbol as the last field of a line. */
static bool defines_symbol(build_t *b, const char *nm, const char *elf,
                           const char *symbol) {
  jb_vector_t arguments = {0};
  jb_string_t listing = {0};
  bool found = false;
  if (storage(b, jh_list_init(&arguments)) &&
      storage(b, jb_string_init(&listing, jb_heap_allocator())) &&
      push(b, &arguments, JH_PARTS(jh_span("--defined-only"))) &&
      push(b, &arguments, JH_PARTS(jh_span(elf))) &&
      run(b, nm, &arguments, &listing)) {
    const char *line = jb_string_data(&listing);
    const size_t wanted = strlen(symbol);
    while (*line != '\0' && !found) {
      const char *end = strchr(line, '\n');
      size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
      while (length != 0 &&
             (line[length - 1] == '\r' || line[length - 1] == ' '))
        --length;
      size_t start = length;
      while (start != 0 && line[start - 1] != ' ' && line[start - 1] != '\t')
        --start;
      found =
          length - start == wanted && memcmp(line + start, symbol, wanted) == 0;
      line = end != NULL ? end + 1 : line + strlen(line);
    }
  }
  jb_string_destroy(&listing);
  jh_list_destroy(&arguments);
  if (ok(b) && !found)
    (void)die(b, JH_PARTS(jh_span("Required symbol '"), jh_span(symbol),
                          jh_span("' not found in "), jh_span(elf)));
  return ok(b);
}

/* The output directory of a library below .build/static/<target>/<key>:
 * the device tree key of config (library_key) or the board, which defaults
 * to default_board. NULL with the failure of the runners when the output
 * leaves <root>/.build. */
static const char *static_output(build_t *b, bool tree, const char *config,
                                 bool with_freertos, const char *default_board,
                                 const char **board) {
  const char *key;
  if (tree) {
    key = library_key(b, config, with_freertos);
  } else {
    *board = (*board)[0] != '\0' ? *board : default_board;
    key = *board;
  }
  const jh_library_build_options_t *o = b->options;
  const char *output =
      build_output(b, o->output,
                   text(b, JH_PARTS(jh_span("static/"), jh_span(o->target),
                                    jh_span("/"), jh_span(key))));
  if (!ok(b))
    return NULL;
  if (output == NULL) {
    (void)die(b, JH_PARTS(jh_span("Build output must be inside "),
                          jh_span(o->root), jh_span("/.build")));
    return NULL;
  }
  return safe_output(b, output) ? output : NULL;
}

static bool pico_sdk(build_t *b, jh_library_build_result_t *result,
                     const char *default_board, const char *isa) {
  const jh_library_build_options_t *o = b->options;
  const char *root = o->root;
  const char *sdk =
      o->sdk_dir != NULL
          ? o->sdk_dir
          : environment(b, "JH_PICO_SDK_DIR",
                        text(b, JH_PARTS(jh_span(root),
                                         jh_span("/third_party/pico-sdk"))));
  const char *picotool =
      o->picotool_dir != NULL
          ? o->picotool_dir
          : text(b, JH_PARTS(jh_span(root), jh_span("/third_party/picotool")));
  const char *picotool_build =
      o->picotool_build_dir != NULL
          ? o->picotool_build_dir
          : environment(b, "JH_PICOTOOL_BUILD_DIR",
                        text(b, JH_PARTS(jh_span(root),
                                         jh_span("/.build/tools/picotool"))));
  const char *toolchain = o->toolchain != NULL ? o->toolchain : "";
  const bool riscv = strcmp(isa, "riscv32") == 0;
  if (riscv && toolchain[0] == '\0')
    toolchain =
        environment(b, "JH_RISCV_TOOLCHAIN_DIR",
                    text(b, JH_PARTS(jh_span(root),
                                     jh_span("/third_party/riscv-toolchain"))));
  sdk = rooted(b, sdk);
  picotool = rooted(b, picotool);
  picotool_build = rooted(b, picotool_build);
  toolchain = rooted(b, toolchain);
  const char *config =
      rooted(b, o->project_config != NULL ? o->project_config : "");
  if (!ok(b))
    return false;
  picotool_build = build_output(b, picotool_build, "tools/picotool");
  if (picotool_build == NULL)
    return die(b, JH_PARTS(jh_span("picotool build output must be inside "),
                           jh_span(root), jh_span("/.build")));
  const char *app = "";
  if (o->example != NULL) {
    const char *example = o->example;
    bool separated = false;
    for (const char *c = example; *c != '\0'; ++c)
      separated = separated || jh_build_separator(*c);
    if (separated || strcmp(example, ".") == 0 || strcmp(example, "..") == 0)
      return die(b, JH_PARTS(jh_span("Example must be a directory name under "
                                     "examples/: "),
                             jh_span(example)));
    app = text(
        b, JH_PARTS(jh_span(root), jh_span("/examples/"), jh_span(example)));
    if (kind(app) != JB_FS_DIRECTORY)
      return die(
          b, JH_PARTS(jh_span("Example directory not found: "), jh_span(app)));
    if (config[0] == '\0')
      config = app;
    else if (strcmp(config, app) != 0)
      return die(b, JH_PARTS(jh_span("--example uses its own "
                                     "hal_project_config.h; remove -p or "
                                     "select "),
                             jh_span(app)));
  }
  if (o->library_only && app[0] != '\0')
    return die(b, JH_PARTS(jh_span("--library-only cannot be combined with "
                                   "--example")));
  const int path = device_tree(b, config);
  if (path < 0)
    return false;
  const char *board = o->board;
  const char *output =
      static_output(b, path == 0, config, true, default_board, &board);
  if (output == NULL)
    return false;
  jb_vector_t args = {0}, defines = {0};
  bool success_path = false;
  if (!storage(b, jh_list_init(&args)) || !storage(b, jh_list_init(&defines)))
    goto done;
  for (size_t i = 0; i < jb_vector_size(o->defines) && ok(b); ++i)
    (void)push(b, &defines,
               JH_PARTS(jh_string_span(jh_list_at(o->defines, i))));
  (void)push(b, &args, JH_PARTS(jh_span("--enable")));
  (void)push(b, &args, JH_PARTS(jh_span("--repo-root")));
  (void)push(b, &args, JH_PARTS(jh_span(root)));
  (void)push(b, &args, JH_PARTS(jh_span("--sdk-dir")));
  (void)push(b, &args, JH_PARTS(jh_span(sdk)));
  if (!ensure(b, "ensure_pico_sdk.sh", "pico-sdk", &args))
    goto done;
  jh_list_destroy(&args);
  if (!storage(b, jh_list_init(&args)))
    goto done;
  (void)push(b, &args, JH_PARTS(jh_span("--enable")));
  (void)push(b, &args, JH_PARTS(jh_span("--repo-root")));
  (void)push(b, &args, JH_PARTS(jh_span(root)));
  (void)push(b, &args, JH_PARTS(jh_span("--picotool-dir")));
  (void)push(b, &args, JH_PARTS(jh_span(picotool)));
  (void)push(b, &args, JH_PARTS(jh_span("--build-dir")));
  (void)push(b, &args, JH_PARTS(jh_span(picotool_build)));
  (void)push(b, &args, JH_PARTS(jh_span("--sdk-dir")));
  (void)push(b, &args, JH_PARTS(jh_span(sdk)));
  if (!ensure(b, "ensure_picotool.sh", "picotool", &args))
    goto done;
  if (o->freertos) {
    jh_list_destroy(&args);
    if (!storage(b, jh_list_init(&args)))
      goto done;
    (void)push(b, &args, JH_PARTS(jh_span("--enable")));
    (void)push(b, &args, JH_PARTS(jh_span("--repo-root")));
    (void)push(b, &args, JH_PARTS(jh_span(root)));
    if (!ensure(b, "ensure_freertos_kernel.sh", "freertos", &args))
      goto done;
    (void)push(b, &defines, JH_PARTS(jh_span("HAL_ENABLE_FREERTOS=1")));
  }
  if (riscv) {
    jh_list_destroy(&args);
    if (!storage(b, jh_list_init(&args)))
      goto done;
    (void)push(b, &args, JH_PARTS(jh_span("--enable")));
    (void)push(b, &args, JH_PARTS(jh_span("--repo-root")));
    (void)push(b, &args, JH_PARTS(jh_span(root)));
    (void)push(b, &args, JH_PARTS(jh_span("--dir")));
    (void)push(b, &args, JH_PARTS(jh_span(toolchain)));
    if (!ensure(b, "ensure_riscv_toolchain.sh", "riscv-toolchain", &args))
      goto done;
  }
  if (!clean_output(b, output))
    goto done;
  jh_list_destroy(&args);
  if (!storage(b, jh_list_init(&args)))
    goto done;
  push_project(b, &args, "rp_pico_lib", output);
  (void)push(b, &args, JH_PARTS(jh_span("-DPICO_SDK_PATH="), jh_span(sdk)));
  push_selection(b, &args, board);
  (void)push(b, &args, JH_PARTS(jh_span("-DCMAKE_BUILD_TYPE=Release")));
  (void)push(b, &args, JH_PARTS(jh_span("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")));
  if (o->all_features)
    (void)push(b, &args, JH_PARTS(jh_span("-DJH_ENABLE_ALL_FEATURES=ON")));
  if (o->library_only) {
    (void)push(b, &args,
               JH_PARTS(jh_span("-DJH_RP_PICO_BUILD_ARTIFACT_PROBE=OFF")));
    (void)push(b, &args,
               JH_PARTS(jh_span("-DJH_RP_PICO_BUILD_CORE1_PROBE=OFF")));
  }
  const char *picotool_executable =
      text(b, JH_PARTS(jh_span(picotool_build), jh_span("/picotool" HOST_EXE)));
  if (ok(b) && !executable(picotool_executable)) {
    (void)die(b, JH_PARTS(jh_span("picotool executable not found: "),
                          jh_span(picotool_executable)));
    goto done;
  }
  (void)push(b, &args,
             JH_PARTS(jh_span("-DJH_PICOTOOL_EXECUTABLE="),
                      jh_span(picotool_executable)));
  if (toolchain[0] != '\0')
    (void)push(b, &args,
               JH_PARTS(jh_span("-DPICO_TOOLCHAIN_PATH="), jh_span(toolchain)));
  if (config[0] != '\0')
    (void)push(b, &args,
               JH_PARTS(jh_span("-DHAL_PROJECT_CONFIG_DIR="), jh_span(config)));
  if (app[0] != '\0')
    (void)push(b, &args,
               JH_PARTS(jh_span("-DJH_RP_PICO_APP_DIR="), jh_span(app)));
  if (jb_vector_size(&defines) != 0)
    (void)push(b, &args,
               JH_PARTS(jh_span("-DEXTRA_HAL_DEFINES="),
                        jh_span(joined_defines(b, &defines))));
  const char *chosen = selection(b, path == 0, board);
  if (!ok(b))
    goto done;
  info(text(b, JH_PARTS(jh_span("Configuring Pico SDK build ("),
                        jh_span(o->target), jh_span(", "), jh_span(chosen),
                        jh_span(")..."))));
  if (!cmake(b, &args) || !build_step(b, output, o->library_only, "--parallel"))
    goto done;
  const char *library =
      text(b, JH_PARTS(jh_span(output), jh_span("/libJaszczurHAL.a")));
  if (!collect_headers(b, output, board, path == 0))
    goto done;
  if (kind(library) != JB_FS_REGULAR) {
    (void)die(
        b, JH_PARTS(jh_span("Static library not found: "), jh_span(library)));
    goto done;
  }
  success(
      text(b, JH_PARTS(jh_span("Pico SDK library built: "), jh_span(library))));
  if (!o->library_only) {
    static const char *const extensions[] = {"elf", "bin", "uf2"};
    static const char *const upper[] = {"ELF", "BIN", "UF2"};
    const char *probe = text(
        b, JH_PARTS(jh_span(output), jh_span("/jh_rp_pico_artifact_probe")));
    const char *core1 =
        text(b, JH_PARTS(jh_span(output), jh_span("/jh_rp_pico_core1_probe")));
    const char *firmware =
        text(b, JH_PARTS(jh_span(output), jh_span("/jh_rp_pico_firmware")));
    const char *bases[] = {probe, core1};
    for (size_t i = 0; i < COUNTOF(bases) && ok(b); ++i)
      for (size_t e = 0; e < COUNTOF(extensions) && ok(b); ++e) {
        const char *artifact = text(b, JH_PARTS(jh_span(bases[i]), jh_span("."),
                                                jh_span(extensions[e])));
        if (kind(artifact) != JB_FS_REGULAR)
          (void)die(b, JH_PARTS(jh_span("Native "), jh_span(upper[e]),
                                jh_span(" artifact not found: "),
                                jh_span(artifact)));
      }
    for (size_t e = 0; app[0] != '\0' && e < COUNTOF(extensions) && ok(b);
         ++e) {
      const char *artifact = text(
          b, JH_PARTS(jh_span(firmware), jh_span("."), jh_span(extensions[e])));
      if (kind(artifact) != JB_FS_REGULAR)
        (void)die(b, JH_PARTS(jh_span("Native example "), jh_span(upper[e]),
                              jh_span(" not found: "), jh_span(artifact)));
    }
    if (!ok(b))
      goto done;
    const char *nm = "";
    if (riscv)
      nm = text(b, JH_PARTS(jh_span(toolchain),
                            jh_span("/bin/riscv32-unknown-elf-nm" HOST_EXE)));
    else {
      jb_string_t found = {0};
      if (storage(b, jb_string_init(&found, jb_heap_allocator()))) {
        jh_failure_t ignored = {0};
        jh_failure_init(&ignored);
        if (jh_process_executable("arm-none-eabi-nm", o->base, false, &found,
                                  &ignored) == HAL_OK)
          nm = text(b, JH_PARTS(jh_string_span(&found)));
        jh_failure_destroy(&ignored);
      }
      jb_string_destroy(&found);
    }
    if (ok(b) && !executable(nm)) {
      (void)die(b, JH_PARTS(jh_span("Target nm tool not found: "),
                            jh_span(nm[0] != '\0' ? nm : "unknown")));
      goto done;
    }
    static const char *const entries[] = {"main", "app_start", "app_task0",
                                          "app_task1"};
    static const char *const scheduler[] = {"vTaskStartScheduler",
                                            "xTaskCreateAffinitySet"};
    static const char *const multicore[] = {"multicore_launch_core1",
                                            "multicore_lockout_victim_init"};
    const char *core1_elf = text(b, JH_PARTS(jh_span(core1), jh_span(".elf")));
    for (size_t i = 0; i < COUNTOF(entries) && ok(b); ++i)
      (void)defines_symbol(b, nm, core1_elf, entries[i]);
    for (size_t i = 0; i < 2 && ok(b); ++i)
      (void)defines_symbol(b, nm, core1_elf,
                           o->freertos ? scheduler[i] : multicore[i]);
    const char *firmware_elf =
        text(b, JH_PARTS(jh_span(firmware), jh_span(".elf")));
    for (size_t i = 0; app[0] != '\0' && i < 3 && ok(b); ++i)
      (void)defines_symbol(b, nm, firmware_elf, entries[i]);
    if (!ok(b))
      goto done;
    success(text(b, JH_PARTS(jh_span("Artifacts: "), jh_span(probe),
                             jh_span(".{elf,bin,uf2}"))));
    success(text(b, JH_PARTS(jh_span("Core-1 entry probe: "), jh_span(core1),
                             jh_span(".{elf,bin,uf2}"))));
    if (app[0] != '\0')
      success(text(b, JH_PARTS(jh_span("Native example "), jh_span(o->example),
                               jh_span(": "), jh_span(firmware),
                               jh_span(".{elf,bin,uf2}"))));
  }
  success_path =
      ok(b) &&
      storage(b, jb_string_assign(&result->output, output, strlen(output))) &&
      storage(b,
              jb_string_assign(&result->library, library, strlen(library))) &&
      storage(b, jb_string_assign(&result->selection, chosen, strlen(chosen)));
  result->device_tree = path == 0;
done:
  jh_list_destroy(&defines);
  jh_list_destroy(&args);
  return success_path;
}

typedef struct {
  const char *found;
  build_t *b;
  jb_string_t path;
} search_t;

/* The first libJaszczurHAL.a below directory, depth first in listing order,
 * as find -print -quit of the STM32 runner returned it. */
static hal_status_t find_library(search_t *search, const char *directory);

static hal_status_t search_entry(void *context, const char *name,
                                 const jb_fs_info_t *info) {
  search_t *search = context;
  if (search->found != NULL)
    return HAL_OK;
  const char *path = text(search->b, JH_PARTS(jh_string_span(&search->path),
                                              jh_span("/"), jh_span(name)));
  if (!ok(search->b))
    return search->b->failure->status;
  if (info->kind == JB_FS_REGULAR && strcmp(name, "libJaszczurHAL.a") == 0) {
    search->found = path;
    return HAL_OK;
  }
  if (info->kind == JB_FS_DIRECTORY) {
    jb_string_t saved = {0};
    hal_status_t status = jb_string_init(&saved, jb_heap_allocator());
    if (status == HAL_OK)
      status = jb_string_assign(&saved, jb_string_data(&search->path),
                                jb_string_size(&search->path));
    if (status == HAL_OK)
      status = find_library(search, path);
    if (status == HAL_OK)
      status = jb_string_assign(&search->path, jb_string_data(&saved),
                                jb_string_size(&saved));
    jb_string_destroy(&saved);
    return status;
  }
  return HAL_OK;
}

static hal_status_t find_library(search_t *search, const char *directory) {
  hal_status_t status =
      jb_string_assign(&search->path, directory, strlen(directory));
  return status == HAL_OK ? jb_fs_list(directory, search_entry, search)
                          : status;
}

static bool stm32(build_t *b, jh_library_build_result_t *result,
                  const char *default_board) {
  const jh_library_build_options_t *o = b->options;
  const char *root = o->root;
  const char *toolchain =
      o->toolchain != NULL
          ? o->toolchain
          : text(b,
                 JH_PARTS(jh_span(root), jh_span("/link_libraries/stm32_lib/"
                                                 "toolchain_stm32g474.cmake")));
  const char *config = o->project_config != NULL ? o->project_config : "";
  const int path = device_tree(b, config);
  if (path < 0)
    return false;
  const char *board = o->board;
  if (path == 0)
    config = canonical(b, config);
  const char *output =
      static_output(b, path == 0, config, true, default_board, &board);
  if (output == NULL)
    return false;
  const char *toolchain_path =
      absolute(toolchain) ? toolchain
                          : text(b, JH_PARTS(jh_span(o->base), jh_span("/"),
                                             jh_span(toolchain)));
  if (ok(b) && kind(toolchain_path) != JB_FS_REGULAR)
    return die(
        b, JH_PARTS(jh_span("Toolchain file not found: "), jh_span(toolchain)));
  {
    jb_string_t found = {0};
    jh_failure_t ignored = {0};
    jh_failure_init(&ignored);
    if (storage(b, jb_string_init(&found, jb_heap_allocator())) &&
        jh_process_executable("arm-none-eabi-g++", o->base, false, &found,
                              &ignored) != HAL_OK) {
      warn("arm-none-eabi-g++ not found in PATH.");
      warn("If you use a custom prefix/path, pass it via CMake cache variables "
           "(e.g. -D ARM_GCC_PREFIX=...).");
    }
    jh_failure_destroy(&ignored);
    jb_string_destroy(&found);
  }
  if (!clean_output(b, output))
    return false;
  if (jb_fs_mkdir(output, true) != HAL_OK)
    return die(b, JH_PARTS(jh_span("cannot create "), jh_span(output)));
  jb_vector_t args = {0}, extra = {0}, defines = {0};
  bool success_path = false;
  search_t search = {NULL, b, {0}};
  if (!storage(b, jh_list_init(&args)) || !storage(b, jh_list_init(&extra)) ||
      !storage(b, jh_list_init(&defines)) ||
      !storage(b, jb_string_init(&search.path, jb_heap_allocator())))
    goto done;
  if (o->all_features)
    (void)push(b, &extra, JH_PARTS(jh_span("-DJH_ENABLE_ALL_FEATURES=ON")));
  if (config[0] != '\0')
    (void)push(b, &extra,
               JH_PARTS(jh_span("-DHAL_PROJECT_CONFIG_DIR="), jh_span(config)));
  bool freertos_defined = false;
  for (size_t i = 0; i < jb_vector_size(o->defines) && ok(b); ++i) {
    const char *definition = jb_string_data(jh_list_at(o->defines, i));
    (void)push(b, &defines, JH_PARTS(jh_span(definition)));
    freertos_defined = freertos_defined ||
                       strcmp(definition, "HAL_ENABLE_FREERTOS") == 0 ||
                       strncmp(definition, "HAL_ENABLE_FREERTOS=", 20) == 0;
  }
  if (o->freertos && !freertos_defined) {
    (void)push(b, &defines, JH_PARTS(jh_span("HAL_ENABLE_FREERTOS")));
    freertos_defined = true;
  }
  if (o->freertos_kernel != NULL)
    (void)push(b, &extra,
               JH_PARTS(jh_span("-DJH_FREERTOS_KERNEL_DIR="),
                        jh_span(o->freertos_kernel)));
  if (freertos_defined) {
    (void)push(b, &args, JH_PARTS(jh_span("--enable")));
    (void)push(b, &args, JH_PARTS(jh_span("--repo-root")));
    (void)push(b, &args, JH_PARTS(jh_span(root)));
    if (o->freertos_kernel != NULL) {
      (void)push(b, &args, JH_PARTS(jh_span("--kernel-dir")));
      (void)push(b, &args, JH_PARTS(jh_span(o->freertos_kernel)));
    }
    if (!ensure(b, "ensure_freertos_kernel.sh", "freertos", &args))
      goto done;
  }
  if (jb_vector_size(&defines) != 0)
    (void)push(b, &extra,
               JH_PARTS(jh_span("-DEXTRA_HAL_DEFINES="),
                        jh_span(joined_defines(b, &defines))));
  const char *chosen = selection(b, path == 0, board);
  if (!ok(b))
    goto done;
  info(text(b, JH_PARTS(jh_span("Configuring CMake ("), jh_span(o->target),
                        jh_span(", "), jh_span(chosen), jh_span(")..."))));
  jh_list_destroy(&args);
  if (!storage(b, jh_list_init(&args)))
    goto done;
  push_project(b, &args, "stm32_lib", output);
  (void)push(b, &args,
             JH_PARTS(jh_span("-DCMAKE_TOOLCHAIN_FILE="), jh_span(toolchain)));
  (void)push(b, &args, JH_PARTS(jh_span("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")));
  push_selection(b, &args, board);
  for (size_t i = 0; i < jb_vector_size(&extra) && ok(b); ++i)
    (void)push(b, &args, JH_PARTS(jh_string_span(jh_list_at(&extra, i))));
  if (!cmake(b, &args) || !build_step(b, output, true, "-j"))
    goto done;
  const char *library =
      text(b, JH_PARTS(jh_span(output), jh_span("/libJaszczurHAL.a")));
  if (ok(b) && kind(library) != JB_FS_REGULAR) {
    library = NULL;
    if (storage(b, find_library(&search, output)))
      library = search.found;
  }
  if (!ok(b))
    goto done;
  if (library == NULL) {
    (void)die(b, JH_PARTS(jh_span("Library not found after build")));
    goto done;
  }
  if (!collect_headers(b, output, board, path == 0))
    goto done;
  jb_fs_info_t entry = {0};
  char size[32] = "?";
  if (jb_fs_status(library, true, &entry) == HAL_OK)
    (void)snprintf(size, sizeof size, "%llu", (unsigned long long)entry.size);
  success(text(b, JH_PARTS(jh_span("Library built: "), jh_span(library),
                           jh_span("  ("), jh_span(size), jh_span(" bytes)"))));
  (void)fprintf(b->log, "\n");
  info(text(
      b, JH_PARTS(jh_span("Headers in: "), jh_span(root), jh_span("/src/"))));
  success_path =
      ok(b) &&
      storage(b, jb_string_assign(&result->output, output, strlen(output))) &&
      storage(b,
              jb_string_assign(&result->library, library, strlen(library))) &&
      storage(b, jb_string_assign(&result->selection, chosen, strlen(chosen)));
  result->device_tree = path == 0;
done:
  jb_string_destroy(&search.path);
  jh_list_destroy(&defines);
  jh_list_destroy(&extra);
  jh_list_destroy(&args);
  return success_path;
}

/* The runner of scripts/build_esp32_lib.sh: the minimal ESP-IDF application
 * of link_libraries/esp32_lib through the ESP-IDF backend, its component
 * archive published as libJaszczurHAL.a next to the generated headers. */
static bool esp_idf(build_t *b, jh_library_build_result_t *result,
                    const char *default_board) {
  const jh_library_build_options_t *o = b->options;
  const char *root = o->root;
  const char *config =
      rooted(b, o->project_config != NULL ? o->project_config : "");
  if (!ok(b))
    return false;
  const int path = device_tree(b, config);
  if (path < 0)
    return false;
  const char *board = o->board;
  /* The device tree key has no FreeRTOS request: ESP-IDF always has it. */
  const char *output =
      static_output(b, path == 0, config, false, default_board, &board);
  if (output == NULL)
    return false;
  jb_vector_t defines = {0}, none = {0};
  jh_esp_result_t built;
  jh_esp_result_init(&built);
  bool success_path = false;
  if (!storage(b, jh_list_init(&defines)) || !storage(b, jh_list_init(&none)))
    goto done;
  for (size_t i = 0; i < jb_vector_size(o->defines) && ok(b); ++i) {
    const char *definition = jb_string_data(jh_list_at(o->defines, i));
    (void)push(
        b, &defines,
        JH_PARTS(jh_span(strncmp(definition, "-D", 2) == 0 ? definition + 2
                                                           : definition)));
  }
  const char *chosen = selection(b, path == 0, board);
  if (!ok(b))
    goto done;
  info(text(b,
            JH_PARTS(jh_span("Building ESP-IDF library ("), jh_span(o->target),
                     jh_span(", "), jh_span(chosen), jh_span(")..."))));
  jh_esp_options_t esp = {0};
  esp.root = root;
  esp.base = o->base;
  esp.action = JH_ESP_ACTION_BUILD;
  esp.project =
      text(b, JH_PARTS(jh_span(root), jh_span("/link_libraries/esp32_lib")));
  esp.target = o->target;
  esp.board = path == 0 ? "" : board;
  esp.assembly = path == 0 ? o->assembly : "";
  esp.variant = path == 0 ? o->variant : "";
  esp.name = "";
  esp.output = output;
  esp.idf_dir = o->idf_dir != NULL ? o->idf_dir : "";
  esp.port = "";
  esp.project_config = config;
  esp.sources = &none;
  esp.include_dirs = &none;
  esp.link_libraries = &none;
  esp.defines = &defines;
  esp.clean = o->clean;
  esp.all_features = o->all_features;
  esp.log_to_stderr = o->log_to_stderr;
  esp.canceled = o->canceled;
  esp.cancel_context = o->cancel_context;
  if (!ok(b) || jh_esp_run(&esp, &built, b->failure) != HAL_OK)
    goto done;
  const char *archive =
      text(b, JH_PARTS(jh_span(output),
                       jh_span("/esp-idf/jaszczurhal/libjaszczurhal.a")));
  if (ok(b) && kind(archive) != JB_FS_REGULAR) {
    (void)die(b, JH_PARTS(jh_span("ESP-IDF component archive not found: "),
                          jh_span(archive)));
    goto done;
  }
  const char *library =
      text(b, JH_PARTS(jh_span(output), jh_span("/libJaszczurHAL.a")));
  if (!copy_file(b, archive, library))
    goto done;
  const char *source =
      text(b, JH_PARTS(jh_span(output), jh_span("/generated/jaszczurhal")));
  const char *include =
      text(b, JH_PARTS(jh_span(output), jh_span("/include/generated")));
  if (ok(b) && jb_fs_mkdir(include, true) != HAL_OK) {
    (void)die(b, JH_PARTS(jh_span("cannot create "), jh_span(include)));
    goto done;
  }
  static const char *const headers[] = {"jh_board_config.h",
                                        "jh_link_contract.h", "jh_hardware.h"};
  if (path != 0) {
    const char *stale =
        text(b, JH_PARTS(jh_span(include), jh_span("/jh_hardware.h")));
    const hal_status_t removed = ok(b) ? jb_fs_remove(stale, false) : HAL_OK;
    if (removed != HAL_OK && removed != HAL_ENOENT) {
      (void)jh_fail(b->failure, removed, 1,
                    JH_PARTS(jh_span("cannot remove "), jh_span(stale)));
      goto done;
    }
  }
  for (size_t i = 0; i < (path == 0 ? 3u : 2u) && ok(b); ++i) {
    const char *from =
        text(b, JH_PARTS(jh_span(source), jh_span("/"), jh_span(headers[i])));
    if (ok(b) && kind(from) != JB_FS_REGULAR) {
      (void)die(b, JH_PARTS(jh_span("Generated board header not found: "),
                            jh_span(from)));
      break;
    }
    (void)copy_mode(
        b, from,
        text(b, JH_PARTS(jh_span(include), jh_span("/"), jh_span(headers[i]))),
        0644);
  }
  if (!ok(b))
    goto done;
  success(
      text(b, JH_PARTS(jh_span("ESP-IDF library built: "), jh_span(library))));
  success(text(b, JH_PARTS(jh_span("Generated headers: "), jh_span(include))));
  success_path =
      ok(b) &&
      storage(b, jb_string_assign(&result->output, output, strlen(output))) &&
      storage(b,
              jb_string_assign(&result->library, library, strlen(library))) &&
      storage(b, jb_string_assign(&result->selection, chosen, strlen(chosen)));
  result->device_tree = path == 0;
done:
  jh_esp_result_destroy(&built);
  jh_list_destroy(&none);
  jh_list_destroy(&defines);
  return success_path;
}

void jh_library_build_result_init(jh_library_build_result_t *result) {
  memset(result, 0, sizeof *result);
  (void)jb_string_init(&result->output, jb_heap_allocator());
  (void)jb_string_init(&result->library, jb_heap_allocator());
  (void)jb_string_init(&result->selection, jb_heap_allocator());
}

void jh_library_build_result_destroy(jh_library_build_result_t *result) {
  jb_string_destroy(&result->output);
  jb_string_destroy(&result->library);
  jb_string_destroy(&result->selection);
  memset(result, 0, sizeof *result);
}

hal_status_t jh_library_build(const jh_library_build_options_t *options,
                              const jh_board_registry_t *registry,
                              jh_library_build_result_t *result,
                              jh_failure_t *failure) {
  if (options == NULL || registry == NULL || result == NULL ||
      failure == NULL || options->root == NULL || options->base == NULL ||
      options->target == NULL || options->board == NULL ||
      options->assembly == NULL || options->variant == NULL ||
      options->defines == NULL || options->output == NULL)
    return jh_fail_arguments(failure);
  build_t b = {
      options, failure, {0}, NULL, options->log_to_stderr ? stderr : stdout};
  progress = b.log;
  if (!storage(&b, jh_list_init(&b.strings)))
    return failure->status;
  if (validate_defines(&b)) {
    const jh_board_descriptor_t *target =
        jh_board_registry_find(&registry->targets, jh_span(options->target));
    if (target == NULL)
      (void)die(&b, JH_PARTS(jh_span("Unknown target '"),
                             jh_span(options->target), jh_span("'")));
    else {
      const jb_json_span_t provider =
          jh_build_descriptor_text(target->root, "build", "provider");
      const char *board = text(&b, JH_PARTS(jh_build_descriptor_text(
                                       target->root, "defaultBoard", NULL)));
      const char *isa = text(&b, JH_PARTS(jh_build_descriptor_text(
                                     target->root, "architecture", "isa")));
      const char *name = text(&b, JH_PARTS(provider));
      if (!ok(&b)) {
      } else if (strcmp(name, "pico-sdk") == 0)
        (void)pico_sdk(&b, result, board, isa);
      else if (strcmp(name, "jh-stm32-baremetal") == 0)
        (void)stm32(&b, result, board);
      else if (strcmp(name, "esp-idf") == 0)
        (void)esp_idf(&b, result, board);
      else
        (void)die(&b, JH_PARTS(jh_span("Target '"), jh_span(options->target),
                               jh_span("' (provider '"), provider,
                               jh_span("') has no linkable-library runner")));
    }
  }
  jh_list_destroy(&b.strings);
  return failure->status;
}

hal_status_t jh_library_install(const char *build_dir, const char *prefix,
                                const char *base, bool log_to_stderr,
                                jb_cancel_fn canceled, void *cancel_context,
                                jh_failure_t *failure) {
  if (build_dir == NULL || prefix == NULL || base == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jh_library_build_options_t options = {0};
  options.base = base;
  options.log_to_stderr = log_to_stderr;
  options.canceled = canceled;
  options.cancel_context = cancel_context;
  build_t b = {&options, failure, {0}, NULL, log_to_stderr ? stderr : stdout};
  progress = b.log;
  jb_vector_t args = {0};
  if (!storage(&b, jh_list_init(&b.strings)) ||
      !storage(&b, jh_list_init(&args)))
    goto done;
  const char *rules =
      text(&b, JH_PARTS(jh_span(build_dir), jh_span("/cmake_install.cmake")));
  if (ok(&b) && kind(rules) != JB_FS_REGULAR) {
    (void)jh_fail(
        failure, HAL_ENOENT, 2,
        JH_PARTS(jh_span(build_dir),
                 jh_span(" has no CMake install rules (cmake_install.cmake)")));
    goto done;
  }
  (void)push(&b, &args, JH_PARTS(jh_span("--install")));
  (void)push(&b, &args, JH_PARTS(jh_span(build_dir)));
  (void)push(&b, &args, JH_PARTS(jh_span("--prefix")));
  (void)push(&b, &args, JH_PARTS(jh_span(prefix)));
  if (!cmake(&b, &args))
    goto done;
  const char *archive =
      text(&b, JH_PARTS(jh_span(prefix), jh_span("/lib/libJaszczurHAL.a")));
  if (ok(&b) && kind(archive) != JB_FS_REGULAR)
    (void)die(&b,
              JH_PARTS(jh_span("installed library archive was not generated: "),
                       jh_span(archive)));
done:
  jh_list_destroy(&args);
  jh_list_destroy(&b.strings);
  return failure->status;
}

static int compare_texts(const void *left, const void *right) {
  return strcmp(*(const char *const *)left, *(const char *const *)right);
}

hal_status_t jh_library_key(const char *config_dir, const char *base,
                            const char *assembly, const jb_vector_t *requests,
                            jb_string_t *out, jh_failure_t *failure) {
  if (config_dir == NULL || assembly == NULL || requests == NULL ||
      out == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jb_string_t config = {0};
  jb_vector_t sorted = {0};
  hal_status_t status = jb_string_init(&config, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_vector_init(&sorted, sizeof(const char *), jb_heap_allocator());
  if (status != HAL_OK) {
    (void)jh_fail_storage(failure, status);
    goto done;
  }
  status = jh_python_resolve(config_dir, base, &config, failure);
  if (status != HAL_OK)
    goto done;
  for (size_t i = 0; i < jb_vector_size(requests) && status == HAL_OK; ++i) {
    const char *item = jb_string_data(jh_list_at(requests, i));
    status = jb_vector_append(&sorted, &item, 1);
  }
  if (status != HAL_OK) {
    (void)jh_fail_storage(failure, status);
    goto done;
  }
  if (jb_vector_size(&sorted) > 1)
    qsort(jb_vector_data(&sorted), jb_vector_size(&sorted),
          sizeof(const char *), compare_texts);
  jb_sha256_context_t sha = {0};
  jb_sha256_init(&sha);
  /* Path.as_posix(): only Windows paths use \\ as a separator. */
  for (const char *c = jb_string_data(&config); *c != '\0'; ++c) {
    const uint8_t byte = (uint8_t)(jh_build_separator(*c) ? '/' : *c);
    jb_sha256_update(&sha, &byte, 1);
  }
  const uint8_t separator = 0;
  jb_sha256_update(&sha, &separator, 1);
  jb_sha256_update(&sha, (const uint8_t *)assembly, strlen(assembly));
  for (size_t i = 0; i < jb_vector_size(&sorted); ++i) {
    const char *item = *(const char *const *)jb_vector_const_at(&sorted, i);
    jb_sha256_update(&sha, &separator, 1);
    jb_sha256_update(&sha, (const uint8_t *)item, strlen(item));
  }
  uint8_t digest[JB_SHA256_DIGEST_BYTES];
  jb_sha256_final(&sha, digest);
  char hex[65];
  jh_sha256_hex(digest, hex);
  hex[16] = '\0';
  jb_string_clear(out);
  jh_put(out, &status, assembly[0] != '\0' ? assembly : "auto");
  jh_put(out, &status, "/");
  jh_put(out, &status, hex);
  if (status != HAL_OK)
    (void)jh_fail_storage(failure, status);
done:
  jb_vector_destroy(&sorted);
  jb_string_destroy(&config);
  return status != HAL_OK && failure->status == HAL_OK
             ? jh_fail_storage(failure, status)
             : failure->status;
}

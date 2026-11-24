/* The build and artifacts actions of scripts/build_esp_idf.py: stages,
 * version pin, managed components (scripts/ensure_*.sh until stage 6), SDK
 * preparation, the exported ESP-IDF environment, idf.py reconfigure,
 * cmake --build, the device tree ABI pass and the failure diagnostic. C11. */
#include "jh_esp_internal.h"
#include "jh_failure.h"
#include "jh_json_file.h"
#include "jh_process_support.h"
#include <jb/filesystem.h>
#include <stdlib.h>
#include <string.h>

#define FAILURE_NAME "jh_esp_idf_failure.txt"
#define LOG_NAME "build.log"
#define TEMPORARY_NAME "tmp"
/* State file of idf_tools.py export, kept in the build directory. */
#define EXPORT_STATE_NAME "jh_idf_tools_export.json"

void jh_esp_result_init(jh_esp_result_t *result) {
  memset(result, 0, sizeof *result);
  (void)jb_string_init(&result->build_dir, jb_heap_allocator());
  (void)jb_string_init(&result->target, jb_heap_allocator());
  (void)jb_string_init(&result->board, jb_heap_allocator());
  (void)jb_string_init(&result->assembly, jb_heap_allocator());
}

void jh_esp_result_destroy(jh_esp_result_t *result) {
  jb_string_destroy(&result->build_dir);
  jb_string_destroy(&result->target);
  jb_string_destroy(&result->board);
  jb_string_destroy(&result->assembly);
  memset(result, 0, sizeof *result);
}

/* ---- Version pin (component_manager.parse_config) ---- */

typedef struct {
  const char *ref, *version, *targets, *directory;
} pin_t;

/* shlex.split(raw, comments=True, posix=True) of one value: the words of
 * raw, quotes and escapes removed, a comment ending it. */
static bool config_words(jh_esp_t *e, const char *raw, jb_vector_t *words) {
  jb_string_t word = {0};
  if (!esp_storage(e, jb_string_init(&word, jb_heap_allocator())))
    return false;
  bool active = false;
  hal_status_t status = HAL_OK;
  for (const char *c = raw; *c != '\0' && status == HAL_OK && esp_ok(e);) {
    if (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') {
      if (active)
        (void)esp_push(e, words, jh_string_span(&word));
      jb_string_clear(&word);
      active = false;
      ++c;
    } else if (*c == '#') {
      break;
    } else if (*c == '\\') {
      if (c[1] == '\0') {
        (void)ESP_FAIL(e, jh_span("No escaped character"));
        break;
      }
      status = jb_string_append(&word, c + 1, 1);
      active = true;
      c += 2;
    } else if (*c == '\'' || *c == '"') {
      const char quote = *c++;
      while (*c != quote && *c != '\0' && status == HAL_OK) {
        if (quote == '"' && *c == '\\' && (c[1] == '"' || c[1] == '\\'))
          ++c;
        status = jb_string_append(&word, c, 1);
        ++c;
      }
      if (*c != quote) {
        (void)ESP_FAIL(e, jh_span("No closing quotation"));
        break;
      }
      ++c;
      active = true;
    } else {
      status = jb_string_append(&word, c, 1);
      active = true;
      ++c;
    }
  }
  if (esp_storage(e, status) && active)
    (void)esp_push(e, words, jh_string_span(&word));
  jb_string_destroy(&word);
  return esp_ok(e);
}

static bool config_key(jb_json_span_t key) {
  if (key.length == 0 || !jh_ascii_is_upper(key.data[0]))
    return false;
  for (size_t i = 1; i < key.length; ++i)
    if (!jh_ascii_is_upper(key.data[i]) && !jh_ascii_is_digit(key.data[i]) &&
        key.data[i] != '_')
      return false;
  return true;
}

/* _pin_config: the values of third_party/esp_idf_version.conf. */
static bool load_pin(jh_esp_t *e, pin_t *pin) {
  const char *path = esp_join(e, esp_join(e, e->repo_root, "third_party"),
                              "esp_idf_version.conf");
  if (esp_ok(e) && !esp_is_file(path))
    return ESP_FAIL(e, jh_span("Component config not found: "), jh_span(path));
  jb_file_data_t data = {0};
  jb_string_t text = {0};
  jb_vector_t keys = {0}, values = {0}, words = {0};
  if (!esp_read(e, path, &data) ||
      !esp_storage(e, jb_string_init(&text, jb_heap_allocator())) ||
      !esp_list_init(e, &keys) || !esp_list_init(e, &values) ||
      !esp_list_init(e, &words) ||
      !esp_decode_text(
          e, (jb_json_span_t){(const char *)data.data, data.length}, &text))
    goto done;
  const char *cursor = jb_string_data(&text);
  for (size_t number = 1; *cursor != '\0' && esp_ok(e); ++number) {
    const char *end = strchr(cursor, '\n');
    const size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
    const jb_json_span_t raw_line = {cursor, length};
    cursor += length + (end != NULL ? 1u : 0u);
    const jb_json_span_t line = esp_strip(raw_line);
    if (line.length == 0 || line.data[0] == '#')
      continue;
    const char *equal = memchr(line.data, '=', line.length);
    const char *where = ESP_TEXT(e, jh_span(path), jh_span(":"),
                                 jh_span(esp_decimal(e, number)));
    if (equal == NULL) {
      (void)ESP_FAIL(e, jh_span("Invalid config line "), jh_span(where));
      break;
    }
    const jb_json_span_t key =
        esp_strip((jb_json_span_t){line.data, (size_t)(equal - line.data)});
    if (!config_key(key)) {
      (void)ESP_FAIL(e, jh_span("Invalid config key "), jh_span(where),
                     jh_span(": "), key);
      break;
    }
    jh_list_destroy(&words);
    (void)jh_list_init(&words);
    const char *value = ESP_TEXT(
        e, ((jb_json_span_t){equal + 1,
                             line.length - (size_t)(equal + 1 - line.data)}));
    jh_failure_t error;
    jh_failure_init(&error);
    jh_failure_t *saved = e->failure;
    e->failure = &error;
    (void)config_words(e, value, &words);
    e->failure = saved;
    if (error.status != HAL_OK)
      (void)ESP_FAIL(e, jh_span("Invalid config value "), jh_span(where),
                     jh_span(": "), jh_string_span(&error.message));
    jh_failure_destroy(&error);
    if (!esp_ok(e))
      break;
    if (jb_vector_size(&words) > 1) {
      (void)ESP_FAIL(e, jh_span("Config value must be quoted at "),
                     jh_span(where));
      break;
    }
    const jb_json_span_t parsed = jb_vector_size(&words) != 0
                                      ? jh_string_span(jh_list_at(&words, 0))
                                      : jh_span("");
    bool replaced = false;
    for (size_t i = 0; i < jb_vector_size(&keys) && !replaced; ++i)
      if (jh_span_equal(jh_string_span(jh_list_at(&keys, i)), key)) {
        jb_string_t *slot = jb_vector_at(&values, i);
        replaced =
            esp_storage(e, jb_string_assign(slot, parsed.data, parsed.length));
      }
    if (!replaced) {
      (void)esp_push(e, &keys, key);
      (void)esp_push(e, &values, parsed);
    }
  }
  static const char *const required[] = {"ESP_IDF_REF", "ESP_IDF_VERSION",
                                         "ESP_IDF_TARGETS"};
  const char *found[4] = {"", "", "", ""};
  static const char *const names[] = {"ESP_IDF_REF", "ESP_IDF_VERSION",
                                      "ESP_IDF_TARGETS", "ESP_IDF_DIR"};
  for (size_t n = 0; n < COUNTOF(names); ++n)
    for (size_t i = 0; i < jb_vector_size(&keys); ++i)
      if (strcmp(esp_at(&keys, i), names[n]) == 0)
        found[n] = ESP_TEXT(e, jh_string_span(jh_list_at(&values, i)));
  for (size_t n = 0; n < COUNTOF(required) && esp_ok(e); ++n)
    if (found[n][0] == '\0')
      (void)ESP_FAIL(e, jh_span(required[n]), jh_span(" missing in "),
                     jh_span(path));
  pin->ref = found[0];
  pin->version = found[1];
  pin->targets = found[2];
  pin->directory = found[3];
done:
  jh_list_destroy(&words);
  jh_list_destroy(&values);
  jh_list_destroy(&keys);
  jb_string_destroy(&text);
  jb_file_release(&data);
  return esp_ok(e);
}

/* ---- Managed components ---- */

/* The managed component name with the component manager: on POSIX its
 * scripts/ensure_<name>.sh launcher, on Windows the manager those scripts
 * start. Its stdout and stderr are kept for the caller, so a failed check
 * stays silent and a failure reports the manager's message. */
static hal_status_t ensure(jh_esp_t *e, const char *name, bool verify_only,
                           bool enable, const char *directory,
                           const jb_vector_t *environment, jb_string_t *out,
                           jb_string_t *err, int *code) {
  jb_vector_t arguments = {0};
  hal_status_t status = HAL_EINVAL;
  if (!esp_list_init(e, &arguments))
    return e->failure->status;
  const char *program;
#ifdef _WIN32
  program = esp_host_python(e);
  (void)esp_push(e, &arguments,
                 jh_span(esp_join(e, esp_join(e, e->repo_root, "scripts"),
                                  "component_manager.py")));
  (void)esp_push(e, &arguments, jh_span("component"));
  (void)esp_push(e, &arguments, jh_span(name));
#else
  jb_string_t script = {0};
  if (esp_storage(e, jb_string_init(&script, jb_heap_allocator())) &&
      esp_storage(e, jh_append(&script, "ensure_")) &&
      esp_storage(e, jh_append(&script, name)) &&
      esp_storage(e, jh_append(&script, ".sh")))
    for (char *c = jb_string_mutable_data(&script); *c != '\0'; ++c)
      if (*c == '-')
        *c = '_';
  program = esp_join(e, esp_join(e, e->repo_root, "scripts"),
                     jb_string_data(&script));
  jb_string_destroy(&script);
#endif
  (void)esp_push(e, &arguments, jh_span("--repo-root"));
  (void)esp_push(e, &arguments, jh_span(e->repo_root));
  if (verify_only)
    (void)esp_push(e, &arguments, jh_span("--verify-only"));
  if (enable)
    (void)esp_push(e, &arguments, jh_span("--enable"));
  if (directory != NULL && directory[0] != '\0') {
    (void)esp_push(e, &arguments, jh_span("--dir"));
    (void)esp_push(e, &arguments, jh_span(directory));
  }
  if (esp_ok(e)) {
    const esp_process_t process = {
        program, &arguments, NULL, environment, ESP_RUN_CAPTURE,
        false,   false,      0,    out,         err};
    status = esp_process(e, &process, code);
  }
  jh_list_destroy(&arguments);
  return status;
}

static void forward(jh_esp_t *e, FILE *file, const jb_string_t *text) {
  if (jb_string_size(text) != 0) {
    (void)fwrite(jb_string_data(text), 1, jb_string_size(text), file);
    (void)fflush(file);
  }
  (void)e;
}

/* The failure of a component manager run: its "[ERROR] message" line. */
static bool ensure_failure(jh_esp_t *e, const char *name, hal_status_t status,
                           int code, const jb_string_t *err) {
  if (!esp_ok(e))
    return false;
  const char *text = jb_string_data(err);
  const char *marker = NULL;
  for (const char *c = strstr(text, "[ERROR] "); c != NULL;
       c = strstr(c + 1, "[ERROR] "))
    marker = c;
  if (status != HAL_OK)
    return ESP_FAIL(e, jh_span("cannot run the component manager for "),
                    jh_span(name), jh_span(": "),
                    jh_span(hal_status_to_string(status)));
  if (marker != NULL) {
    /* Everything before the manager's message is still shown. */
    (void)fwrite(text, 1, (size_t)(marker - text), stderr);
    (void)fflush(stderr);
    /* The message, also a multi-line one, runs to the end of the output. */
    jb_json_span_t message = {marker + 8, strlen(marker + 8)};
    while (message.length != 0 && (message.data[message.length - 1] == '\n' ||
                                   message.data[message.length - 1] == '\r'))
      --message.length;
    return ESP_FAIL(e, message);
  }
  forward(e, stderr, err);
  char number[16];
  (void)snprintf(number, sizeof number, "%d", code);
  return ESP_FAIL(e, jh_span("component manager for "), jh_span(name),
                  jh_span(" failed with exit code "), jh_span(number));
}

static bool ensure_component(jh_esp_t *e, const char *name, bool verify_only) {
  jb_string_t out = {0}, err = {0};
  int code = -1;
  if (esp_storage(e, jb_string_init(&out, jb_heap_allocator())) &&
      esp_storage(e, jb_string_init(&err, jb_heap_allocator()))) {
    const hal_status_t status =
        ensure(e, name, verify_only, false, NULL, NULL, &out, &err, &code);
    forward(e, e->log, &out);
    if (status == HAL_OK && code == 0)
      forward(e, stderr, &err);
    else
      (void)ensure_failure(e, name, status, code, &err);
  }
  jb_string_destroy(&err);
  jb_string_destroy(&out);
  return esp_ok(e);
}

/* prepare_feature_dependencies */
static bool feature_dependencies(jh_esp_t *e, bool verify_only) {
  const jh_esp_model_t *m = &e->model;
  for (size_t i = 0; i < jb_vector_size(&m->managed_frameworks) && esp_ok(e);
       ++i)
    (void)ensure_component(e, esp_at(&m->managed_frameworks, i), verify_only);
  if (esp_ok(e) && jh_list_contains(&m->resolved, jh_span("HAL_ENABLE_TLS")))
    (void)ensure_component(e, "bearssl", verify_only);
  return esp_ok(e);
}

/* prepare_sdk: verify the pinned SDK and its tools, otherwise synchronize
 * them and verify again (local edits stay in place; build only from the
 * pinned sources). */
static bool prepare_sdk(jh_esp_t *e, const pin_t *pin) {
  const char *override = e->options->idf_dir;
  jb_vector_t environment = {0};
  jb_string_t value = {0}, out = {0}, err = {0};
  bool found = false;
  if (!esp_environment_capture(e, &environment) ||
      !esp_storage(e, jb_string_init(&value, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&out, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&err, jb_heap_allocator())))
    goto done;
  if (override[0] == '\0' &&
      esp_storage(e,
                  jh_process_environment("JH_ESP_IDF_DIR", &value, &found)) &&
      found)
    override = ESP_TEXT(e, jh_string_span(&value));
  /* The manager prefers JH_ESP_IDF_DIR over --dir; give both the same. */
  if (override[0] != '\0')
    (void)esp_environment_set(e, &environment, "JH_ESP_IDF_DIR", override);
  const char *configured = override[0] != '\0' ? override : pin->directory;
  const char *candidate = esp_expand_user(e, configured);
  e->idf_dir = esp_resolve(e,
                           esp_absolute(candidate)
                               ? candidate
                               : esp_join(e, e->repo_root, candidate),
                           e->options->base);
  if (!esp_ok(e))
    goto done;
  int code = -1;
  hal_status_t status = ensure(e, "esp-idf", true, false, override,
                               &environment, &out, &err, &code);
  if (!esp_ok(e))
    goto done;
  if (status == HAL_OK && code == 0) {
    forward(e, e->log, &out);
    forward(e, stderr, &err);
    goto done;
  }
  const bool steps[] = {false, true};
  for (size_t i = 0; i < COUNTOF(steps) && esp_ok(e); ++i) {
    jb_string_clear(&out);
    jb_string_clear(&err);
    status = ensure(e, "esp-idf", steps[i], !steps[i], override, &environment,
                    &out, &err, &code);
    forward(e, e->log, &out);
    if (status == HAL_OK && code == 0)
      forward(e, stderr, &err);
    else
      (void)ensure_failure(e, "esp-idf", status, code, &err);
  }
done:
  jb_string_destroy(&err);
  jb_string_destroy(&out);
  jb_string_destroy(&value);
  jh_list_destroy(&environment);
  return esp_ok(e);
}

static bool export_key(jb_json_span_t key) { return config_key(key); }

/* exported_environment: the environment of idf_tools.py export. */
static bool export_environment(jh_esp_t *e) {
  jb_vector_t baseline = {0}, arguments = {0}, export_env = {0};
  jb_string_t out = {0}, err = {0}, text = {0}, error_text = {0};
  const char *state = NULL;
  if (!esp_environment_capture(e, &baseline) || !esp_list_init(e, &arguments) ||
      !esp_list_init(e, &export_env) ||
      !esp_storage(e, jb_string_init(&out, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&err, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&text, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&error_text, jb_heap_allocator())))
    goto done;
  /* component_manager.python_environment() */
  const char *cache = esp_environment_get(&baseline, "PYTHONPYCACHEPREFIX");
  if (cache == NULL || cache[0] == '\0')
    (void)esp_environment_set(
        e, &baseline, "PYTHONPYCACHEPREFIX",
        esp_join(e, esp_join(e, e->repo_root, ".build"), "python-cache"));
  const char *python = esp_host_python(e);
  (void)esp_push(
      e, &arguments,
      jh_span(esp_join(e, esp_join(e, e->idf_dir, "tools"), "idf_tools.py")));
  (void)esp_push(e, &arguments, jh_span("--idf-path"));
  (void)esp_push(e, &arguments, jh_span(e->idf_dir));
  (void)esp_push(e, &arguments, jh_span("export"));
  (void)esp_push(e, &arguments, jh_span("--format"));
  (void)esp_push(e, &arguments, jh_span("key-value"));
  /* The export prints paths of the user profile; UTF-8 keeps non-ASCII
   * names intact where Python would write the ANSI code page of Windows.
   * Only this child gets it: later processes keep the baseline. */
  for (size_t i = 0; i < jb_vector_size(&baseline) && esp_ok(e); ++i)
    (void)esp_push(e, &export_env, jh_string_span(jh_list_at(&baseline, i)));
  (void)esp_environment_set(e, &export_env, "PYTHONIOENCODING", "utf-8");
  /* The export saves the variables a shell removes on deactivation to
   * IDF_DEACTIVATE_FILE_PATH, without it to a new temporary file nothing
   * deletes. A build has no shell to deactivate: the file stays in the build
   * directory, apart from the caller's shell, and goes after the export. */
  state = esp_join(e, e->build_dir, EXPORT_STATE_NAME);
  if (esp_ok(e) && esp_remove(e, state))
    (void)esp_environment_set(e, &export_env, "IDF_DEACTIVATE_FILE_PATH",
                              state);
  if (!esp_ok(e))
    goto done;
  const esp_process_t process = {
      python, &arguments, NULL, &export_env, ESP_RUN_CAPTURE,
      false,  true,       0,    &out,        &err};
  int code = -1;
  const hal_status_t status = esp_process(e, &process, &code);
  if (!esp_ok(e))
    goto done;
  if (status == HAL_OK && esp_decode_text(e, jh_string_span(&err), &error_text))
    forward(e, stderr, &error_text);
  if (status != HAL_OK || code != 0) {
    (void)ESP_FAIL(e, jh_span("ESP-IDF environment export failed"));
    goto done;
  }
  if (!esp_decode_text(e, jh_string_span(&out), &text))
    goto done;
  jh_list_destroy(&e->environment);
  if (!esp_list_init(e, &e->environment))
    goto done;
  e->exported = true;
  for (size_t i = 0; i < jb_vector_size(&baseline) && esp_ok(e); ++i)
    (void)esp_push(e, &e->environment,
                   jh_string_span(jh_list_at(&baseline, i)));
#ifdef _WIN32
  const char *token = "%PATH%";
#else
  const char *token = "$PATH";
#endif
  const char *old_path = esp_environment_get(&baseline, "PATH");
  jb_json_span_t input = jh_string_span(&text);
  while (input.length != 0 && esp_ok(e)) {
    const char *end = memchr(input.data, '\n', input.length);
    const size_t length =
        end != NULL ? (size_t)(end - input.data) : input.length;
    const jb_json_span_t line = {input.data, length};
    input.data += length + (end != NULL ? 1u : 0u);
    input.length -= length + (end != NULL ? 1u : 0u);
    const char *equal = memchr(line.data, '=', line.length);
    if (line.length == 0 || equal == NULL)
      continue;
    const jb_json_span_t key = {line.data, (size_t)(equal - line.data)};
    if (!export_key(key)) {
      (void)ESP_FAIL(e, jh_span("Invalid ESP-IDF environment key: "),
                     esp_repr(e, key));
      break;
    }
    const char *name = ESP_TEXT(e, key);
    const char *value = ESP_TEXT(
        e, ((jb_json_span_t){equal + 1, line.length - key.length - 1}));
    if (strcmp(name, "PATH") == 0) {
      /* str.replace of every occurrence. */
      jb_string_t replaced = {0};
      hal_status_t built = jb_string_init(&replaced, jb_heap_allocator());
      for (const char *c = value; *c != '\0' && built == HAL_OK;) {
        if (strncmp(c, token, strlen(token)) == 0) {
          jh_put(&replaced, &built, old_path != NULL ? old_path : "");
          c += strlen(token);
        } else {
          built = jb_string_append(&replaced, c, 1);
          ++c;
        }
      }
      if (esp_storage(e, built))
        value = ESP_TEXT(e, jh_string_span(&replaced));
      jb_string_destroy(&replaced);
    }
    (void)esp_environment_set(e, &e->environment, name, value);
  }
  if (!esp_ok(e))
    goto done;
  if (esp_environment_get(&e->environment, "IDF_PYTHON_ENV_PATH") == NULL) {
    (void)ESP_FAIL(
        e, jh_span("ESP-IDF export did not provide IDF_PYTHON_ENV_PATH"));
    goto done;
  }
  /* The Windows SDK exports 1 even when the caller opted out; keep that
   * choice so its ccache does not wrap our CMake launcher. */
  const char *ccache = esp_environment_get(&baseline, "IDF_CCACHE_ENABLE");
  if (ccache != NULL)
    (void)esp_environment_set(e, &e->environment, "IDF_CCACHE_ENABLE", ccache);
  (void)esp_environment_set(e, &e->environment, "IDF_PATH", e->idf_dir);
  const char *interpreter = esp_idf_python(e);
  if (esp_ok(e) && !esp_is_file(interpreter))
    (void)ESP_FAIL(e, jh_span("ESP-IDF Python interpreter is missing: "),
                   jh_span(interpreter));
done:
  if (state != NULL)
    (void)esp_remove(e, state);
  jb_string_destroy(&error_text);
  jb_string_destroy(&text);
  jb_string_destroy(&err);
  jb_string_destroy(&out);
  jh_list_destroy(&export_env);
  jh_list_destroy(&arguments);
  jh_list_destroy(&baseline);
  return esp_ok(e);
}

static bool positive_integer(const char *text) {
  if (text[0] < '1' || text[0] > '9')
    return false;
  for (const char *c = text + 1; *c != '\0'; ++c)
    if (!jh_ascii_is_digit(*c))
      return false;
  return true;
}

/* run_idf: idf.py reconfigure, or cmake --build of the configured tree;
 * stdout and stderr go to build.log and the log stream. */
static bool run_idf(jh_esp_t *e, bool build) {
  const char *action = build ? "build" : "reconfigure";
  jb_vector_t arguments = {0}, environment = {0};
  jb_string_t raw = {0}, log = {0};
  const char *program = NULL, *temporary = NULL;
  if (!esp_list_init(e, &arguments) ||
      !esp_storage(e, jb_string_init(&raw, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&log, jb_heap_allocator())))
    goto done;
  const char *jobs =
      esp_environment_get(&e->environment, "CMAKE_BUILD_PARALLEL_LEVEL");
  if (build) {
    /* idf.py configures below; CMake forwards the job limit to Ninja. */
    program = esp_find_program(e, &e->environment, "cmake");
    (void)esp_push(e, &arguments, jh_span("--build"));
    (void)esp_push(e, &arguments, jh_span(e->build_dir));
    if (jobs != NULL && jobs[0] != '\0') {
      if (!positive_integer(jobs)) {
        (void)ESP_FAIL(e,
                       jh_span("CMAKE_BUILD_PARALLEL_LEVEL must be a positive "
                               "integer"));
        goto done;
      }
      (void)esp_push(e, &arguments, jh_span("--parallel"));
      (void)esp_push(e, &arguments, jh_span(jobs));
    }
  } else {
    program = esp_idf_python(e);
    const char *project =
        esp_join(e, esp_join(e, esp_join(e, e->repo_root, "cmake"), "esp-idf"),
                 "project");
    const char *items[] = {
        esp_join(e, esp_join(e, e->idf_dir, "tools"), "idf.py"),
        "-C",
        project,
        "-B",
        e->build_dir,
        "-D",
        ESP_TEXT(e, jh_span("IDF_TARGET="), jh_span(e->model.idf_target)),
        "-D",
        ESP_TEXT(e, jh_span("SDKCONFIG="),
                 jh_span(esp_join(e, e->build_dir, "sdkconfig"))),
        "-D",
        "CMAKE_EXPORT_COMPILE_COMMANDS=ON"};
    for (size_t i = 0; i < COUNTOF(items) && esp_ok(e); ++i)
      (void)esp_push(e, &arguments, jh_span(items[i]));
    static const char *const launchers[] = {"CMAKE_C_COMPILER_LAUNCHER",
                                            "CMAKE_CXX_COMPILER_LAUNCHER"};
    for (size_t i = 0; i < COUNTOF(launchers) && esp_ok(e); ++i) {
      const char *value = esp_environment_get(&e->environment, launchers[i]);
      if (value == NULL)
        continue;
      (void)esp_push(e, &arguments, jh_span("-D"));
      (void)esp_push(e, &arguments,
                     jh_span(ESP_TEXT(e, jh_span(launchers[i]), jh_span("="),
                                      jh_span(value))));
    }
    (void)esp_push(e, &arguments, jh_span(action));
  }
  const hal_status_t made = jb_fs_mkdir(e->build_dir, true);
  if (esp_ok(e) && made != HAL_OK) {
    (void)jh_fail(e->failure, made, 1,
                  JH_PARTS(jh_span("cannot create "), jh_span(e->build_dir)));
    goto done;
  }
  const char *log_path = esp_join(e, e->build_dir, LOG_NAME);
  if (!esp_idf_environment(e, &e->environment, &environment))
    goto done;
  /* The bootloader is an ExternalProject occupying one parent job; its
   * nested cmake --build must not start another full job pool. */
  if (build && jobs != NULL && jobs[0] != '\0')
    (void)esp_environment_set(e, &environment, "CMAKE_BUILD_PARALLEL_LEVEL",
                              "1");
  /* kconfgen of ESP-IDF leaves an empty kconfgen_tmp*.old beside each
   * temporary file it writes. Temporary files of the IDF processes stay in
   * the build directory and go when the run ends. */
  const char *directory = esp_join(e, e->build_dir, TEMPORARY_NAME);
  if (!esp_ok(e))
    goto done;
  (void)jb_fs_remove(directory, true);
  temporary = directory;
  const hal_status_t prepared = jb_fs_mkdir(temporary, false);
  if (prepared != HAL_OK) {
    (void)jh_fail(e->failure, prepared, 1,
                  JH_PARTS(jh_span("cannot create "), jh_span(temporary)));
    goto done;
  }
  (void)esp_environment_set(e, &environment, "TMPDIR", temporary);
  if (!esp_ok(e))
    goto done;
  int code = -1;
  hal_status_t status = HAL_ENOENT;
  if (program != NULL) {
    const esp_process_t process = {
        program, &arguments, e->repo_root, &environment, ESP_RUN_MERGE, true,
        true,    0,          &raw,         NULL};
    status = esp_process(e, &process, &code);
  }
  if (!esp_ok(e))
    goto done;
  if (status != HAL_OK) {
    const char *message =
        status == HAL_ENOENT
            ? ESP_TEXT(
                  e, jh_span("Cannot start ESP-IDF "), jh_span(action),
                  jh_span(": [Errno 2] No such file or directory: "),
                  esp_repr(e, jh_span(program != NULL ? program : "cmake")))
            : ESP_TEXT(e, jh_span("Cannot start ESP-IDF "), jh_span(action),
                       jh_span(": "), jh_span(hal_status_to_string(status)));
    (void)esp_write_text(e, log_path,
                         jh_span(ESP_TEXT(e, jh_span(message), jh_span("\n"))));
    (void)fprintf(e->log, "%s\n", message);
    (void)fflush(e->log);
    (void)ESP_FAIL(e, jh_span(message));
    goto done;
  }
  if (!esp_decode_text(e, jh_string_span(&raw), &log) ||
      !esp_write_text(e, log_path, jh_string_span(&log)))
    goto done;
  if (code != 0) {
    char number[16];
    (void)snprintf(number, sizeof number, "%d", code);
    (void)ESP_FAIL(e, jh_span("ESP-IDF "), jh_span(action),
                   jh_span(" failed with exit code "), jh_span(number));
  }
done:
  if (temporary != NULL)
    (void)jb_fs_remove(temporary, true);
  jh_list_destroy(&environment);
  jb_string_destroy(&log);
  jb_string_destroy(&raw);
  jh_list_destroy(&arguments);
  return esp_ok(e);
}

/* _report_failure: "stage=<stage> failed: <message>", also in the build
 * directory's jh_esp_idf_failure.txt once it is known. */
static void report_failure(jh_esp_t *e, const char *stage) {
  jh_failure_t *failure = e->failure;
  const hal_status_t status = failure->status;
  int exit_code = status == HAL_ENOMEM || failure->exit_code == 4 ? 4
                  : status == HAL_ECANCELED                       ? 130
                                                                  : 1;
  const char *detail = jb_string_size(&failure->message) != 0
                           ? jb_string_data(&failure->message)
                           : hal_status_to_string(status);
  jb_string_t message = {0};
  if (jb_string_init(&message, jb_heap_allocator()) != HAL_OK ||
      jh_join(&message, JH_PARTS(jh_span("stage="), jh_span(stage),
                                 jh_span(" failed: "), jh_span(detail))) !=
          HAL_OK) {
    jb_string_destroy(&message);
    failure->exit_code = exit_code;
    return;
  }
  (void)jh_fail(failure, status, exit_code, JH_PARTS(jh_string_span(&message)));
  if (e->build_dir != NULL) {
    jb_string_t text = {0}, path = {0};
    hal_status_t written = jb_string_init(&text, jb_heap_allocator());
    if (written == HAL_OK)
      written = jb_string_init(&path, jb_heap_allocator());
    if (written == HAL_OK)
      written =
          jh_join(&text, JH_PARTS(jh_span("jh: "), jh_string_span(&message),
                                  jh_span("\n")));
    if (written == HAL_OK)
      written = jh_path_join(e->build_dir, FAILURE_NAME, &path);
    if (written == HAL_OK)
      written = jb_fs_mkdir(e->build_dir, true);
    if (written == HAL_OK) {
      /* Path.write_text() of the runner. */
      jh_failure_t ignored;
      jh_failure_init(&ignored);
      jh_failure_t *saved = e->failure;
      e->failure = &ignored;
      (void)esp_write_text(e, jb_string_data(&path), jh_string_span(&text));
      e->failure = saved;
      written = ignored.status;
      jh_failure_destroy(&ignored);
    }
    if (written != HAL_OK)
      (void)fprintf(stderr, "jh: cannot persist failure diagnostic: %s\n",
                    hal_status_to_string(written));
    jb_string_destroy(&path);
    jb_string_destroy(&text);
  }
  jb_string_destroy(&message);
}

hal_status_t jh_esp_run(const jh_esp_options_t *options,
                        jh_esp_result_t *result, jh_failure_t *failure) {
  if (options == NULL || result == NULL || failure == NULL ||
      options->root == NULL || options->base == NULL ||
      options->project == NULL || options->target == NULL ||
      options->board == NULL || options->assembly == NULL ||
      options->name == NULL || options->output == NULL ||
      options->variant == NULL || options->idf_dir == NULL ||
      options->port == NULL || options->project_config == NULL ||
      options->sources == NULL || options->defines == NULL)
    return jh_fail_arguments(failure);
  jh_esp_t e = {0};
  e.options = options;
  e.failure = failure;
  e.log = options->log_to_stderr ? stderr : stdout;
  jh_board_registry_init(&e.boards);
  jh_feature_registry_init(&e.features);
  jh_hardware_registry_init(&e.hardware);
  esp_model_init(&e.model);
  (void)jh_list_init(&e.environment);
  const char *stage = "resolve-project";
  const bool build = options->action == JH_ESP_ACTION_BUILD;
  size_t flash_images = 0;
  pin_t pin = {"", "", "", ""};
  if (!esp_list_init(&e, &e.strings))
    goto done;
  e.repo_root =
      esp_resolve(&e, esp_expand_user(&e, options->root), options->base);
  const char *project_dir = "", *project_name = "";
  if (!esp_resolve_project_dir(&e, &project_dir) ||
      !esp_project_name(&e, project_dir, &project_name))
    goto done;
  stage = "resolve-build-model";
  if (!esp_load_tables(&e) || !esp_resolve_model(&e, project_dir, project_name))
    goto done;
  stage = "resolve-build-directory";
  if (!esp_resolve_build_dir(&e, project_dir))
    goto done;
  stage = "validate-command";
  if (!build && options->clean) {
    (void)ESP_FAIL(&e,
                   jh_span("--clean is supported only by the build action"));
    goto done;
  }
  if (options->port[0] != '\0') {
    (void)ESP_FAIL(&e, jh_span("--port is supported only by the flash action"));
    goto done;
  }
  stage = "load-version-pin";
  if (!load_pin(&e, &pin))
    goto done;
  e.idf_version = pin.version;
  e.idf_commit = pin.ref;
  {
    bool enabled = false;
    const char *targets = pin.targets;
    const size_t length = strlen(e.model.idf_target);
    for (const char *c = targets; c != NULL && !enabled;) {
      const char *end = strchr(c, ',');
      const size_t item = end != NULL ? (size_t)(end - c) : strlen(c);
      enabled = item == length && strncmp(c, e.model.idf_target, length) == 0;
      c = end != NULL ? end + 1 : NULL;
    }
    if (!enabled) {
      (void)ESP_FAIL(&e, jh_span("ESP-IDF target "),
                     esp_repr(&e, jh_span(e.model.idf_target)),
                     jh_span(" is not enabled by the pin"));
      goto done;
    }
  }
  stage = "prepare-feature-dependencies";
  if (!feature_dependencies(&e, !build))
    goto done;
  if (build) {
    if (options->clean && esp_exists(e.build_dir)) {
      stage = "clean-build-directory";
      const hal_status_t removed = jb_fs_remove(e.build_dir, true);
      if (removed != HAL_OK) {
        (void)jh_fail(
            failure, removed, 1,
            JH_PARTS(jh_span("cannot remove "), jh_span(e.build_dir)));
        goto done;
      }
    }
  }
  stage = "clear-failure-diagnostic";
  if (!esp_remove(&e, esp_join(&e, e.build_dir, FAILURE_NAME)))
    goto done;
  if (build) {
    stage = "materialize-build-inputs";
    if (!esp_materialize(&e))
      goto done;
    stage = "prepare-sdk-for-build";
    if (!prepare_sdk(&e, &pin))
      goto done;
    stage = "export-build-environment";
    if (!export_environment(&e))
      goto done;
    stage = "idf-configure";
    if (!run_idf(&e, false))
      goto done;
    if (e.model.device_tree) {
      /* The configured build names the flags the HAL compiles with;
       * finalize measures their ABI before anything compiles. */
      stage = "device-tree-abi";
      jb_vector_t command = {0};
      if (esp_list_init(&e, &command) && esp_abi_command(&e, &command))
        (void)esp_board_inputs(
            &e, esp_join(&e, e.build_dir, JH_ESP_GENERATED_DIR), &command);
      jh_list_destroy(&command);
      if (!esp_ok(&e))
        goto done;
    }
    stage = "idf-build";
    if (!run_idf(&e, true))
      goto done;
    stage = "toolchain-provenance";
    if (!esp_collect_toolchain(&e))
      goto done;
  }
  stage = "artifact-validation";
  if (!esp_validate_artifacts(&e, &flash_images))
    goto done;
  (void)fprintf(e.log, "ESP-IDF verified: %s:%s with %llu flash images in %s\n",
                e.model.target, e.model.board, (unsigned long long)flash_images,
                e.build_dir);
  (void)fflush(e.log);
  result->device_tree = e.model.device_tree;
  result->flash_images = flash_images;
  (void)(esp_storage(&e, jb_string_assign(&result->build_dir, e.build_dir,
                                          strlen(e.build_dir))) &&
         esp_storage(&e, jb_string_assign(&result->target, e.model.target,
                                          strlen(e.model.target))) &&
         esp_storage(&e, jb_string_assign(&result->board, e.model.board,
                                          strlen(e.model.board))) &&
         esp_storage(&e,
                     jh_append(&result->assembly,
                               e.model.device_tree ? e.model.assembly : "")));
done:
  if (failure->status != HAL_OK)
    report_failure(&e, stage);
  jh_list_destroy(&e.environment);
  esp_model_destroy(&e.model);
  jb_json_tree_destroy(e.tables);
  jh_hardware_registry_destroy(&e.hardware);
  jh_feature_registry_destroy(&e.features);
  jh_board_registry_destroy(&e.boards);
  jh_list_destroy(&e.strings);
  return failure->status;
}

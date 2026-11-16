#include "jh_effective_project.h"
#include "jh_process_support.h"
#include "jh_python_repr.h"
#include <jb/utf8.h>
#include <stdlib.h>
#include <string.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

static bool contains_target_token(jb_json_span_t text) {
  static const char token[] = "${target}";
  for (size_t i = 0; i + sizeof(token) - 1 <= text.length; ++i)
    if (memcmp(text.data + i, token, sizeof(token) - 1) == 0)
      return true;
  return false;
}

static bool target_token(jb_json_tree_view_t value) {
  if (jh_json_type(value) == JB_JSON_STRING) {
    jb_json_span_t text = {0};
    (void)jh_json_string(value, &text);
    if (contains_target_token(text))
      return true;
  }
  for (size_t i = 0; i < jh_json_count(value); ++i)
    if (contains_target_token(jh_json_key(jh_json_item(value, i))) ||
        target_token(jh_json_item(value, i)))
      return true;
  return false;
}

const char *jh_effective_target(jh_hw_t *h, jb_json_tree_view_t manifest,
                                jb_json_tree_view_t settings,
                                const char *target) {
  (void)h;
  if (*target)
    return target;
  static const char *const selectors[] = {"target", "board"};
  for (size_t i = 0; i < COUNTOF(selectors); ++i) {
    const jb_json_tree_view_t value = jh_json_field(manifest, selectors[i]);
    if (value.tree != NULL && jh_json_type(value) != JB_JSON_NULL)
      return "rp2040";
  }
  if (jh_json_type(jh_json_field(manifest, "targetProfiles")) == JB_JSON_OBJECT)
    return "rp2040";
  static const char *const sections[] = {
      "project",       "module", "toolchain", "assembly",  "buildDir",
      "cmakeBuildDir", "cmake",  "identity",  "artifacts", "espIdf",
      "upload",        "ota",    "hooks"};
  for (size_t i = 0; i < COUNTOF(sections); ++i)
    if (target_token(jh_json_field(manifest, sections[i])))
      return "rp2040";
  static const struct {
    const char *setting, *manifest;
  } fallbacks[] = {{"projectName", "project"}, {"moduleName", "module"},
                   {"buildDir", "buildDir"},   {"root", NULL},
                   {"uploadPort", NULL},       {"verbose", NULL},
                   {"vscodeEntry", NULL}};
  for (size_t i = 0; i < COUNTOF(fallbacks); ++i)
    if ((fallbacks[i].manifest == NULL ||
         jh_json_field(manifest, fallbacks[i].manifest).tree == NULL) &&
        target_token(jh_json_field(
            settings, jh_hw_join(h, JH_PARTS(jh_span("jaszczurhal."),
                                             jh_span(fallbacks[i].setting))))))
      return "rp2040";
  static const char *const identity[] = {"usbManufacturer", "usbProduct",
                                         "identityEnabled"};
  for (size_t i = 0; i < COUNTOF(identity); ++i)
    if (jh_json_field(jh_json_field(manifest, "identity"),
                      i == 2 ? "enabled" : identity[i])
                .tree == NULL &&
        target_token(jh_json_field(
            settings, jh_hw_join(h, JH_PARTS(jh_span("jaszczurhal."),
                                             jh_span(identity[i]))))))
      return "rp2040";
  return target;
}

typedef struct {
  pcre2_code *code;
  pcre2_match_data *match;
} decimal_t;

static bool decimal_scalar(jh_hw_t *h, decimal_t *decimal, uint32_t scalar) {
  if (decimal->code == NULL) {
    int error = 0;
    PCRE2_SIZE offset = 0;
    decimal->code =
        pcre2_compile((PCRE2_SPTR) "\\A\\p{Nd}\\z", PCRE2_ZERO_TERMINATED,
                      PCRE2_UTF | PCRE2_UCP, &error, &offset, NULL);
    if (decimal->code != NULL)
      decimal->match =
          pcre2_match_data_create_from_pattern(decimal->code, NULL);
    if (decimal->code == NULL || decimal->match == NULL) {
      jh_node_storage(&h->memory, HAL_ENOMEM);
      return false;
    }
  }
  char bytes[4];
  size_t length = 0;
  if (jb_utf8_encode(scalar, bytes, sizeof bytes, &length) != HAL_OK)
    return false;
  const int result = pcre2_match(decimal->code, (PCRE2_SPTR)bytes, length, 0, 0,
                                 decimal->match, NULL);
  if (result < 0 && result != PCRE2_ERROR_NOMATCH)
    jh_node_storage(&h->memory, result == PCRE2_ERROR_NOMEMORY ? HAL_ENOMEM
                                                               : HAL_EINTERNAL);
  return result >= 0;
}

/* Unicode decimal digits occur in complete, contiguous sets of ten. The
 * mathematical styled digits have adjacent sets, hence modulo ten. */
static int digit(jh_hw_t *h, decimal_t *decimal, uint32_t scalar) {
  if (scalar < 128)
    return jh_hex_digit((char)scalar);
  if (!decimal_scalar(h, decimal, scalar))
    return -1;
  uint32_t first = scalar;
  while (first && h->memory.status == HAL_OK &&
         decimal_scalar(h, decimal, first - 1))
    --first;
  return (int)((scalar - first) % 10);
}

static const char *usb_problem(jh_hw_t *h, decimal_t *decimal,
                               jb_json_tree_view_t value) {
  static const char range[] = " must be in range 0..65535";
  static const char invalid[] =
      " must be an integer or a USB hexadecimal identifier";
  jb_json_span_t text = {0};
  int64_t number = 0;
  if (jh_json_int64(value, &number))
    return number < 0 || number > 65535 ? range : NULL;
  if (jh_json_type(value) == JB_JSON_NUMBER) {
    (void)jb_json_tree_text(value, &text);
    /* Out-of-int64 integer tokens are still Python ints; floats are ignored. */
    if (memchr(text.data, '.', text.length) == NULL &&
        memchr(text.data, 'e', text.length) == NULL &&
        memchr(text.data, 'E', text.length) == NULL &&
        memchr(text.data, 'I', text.length) == NULL &&
        memchr(text.data, 'N', text.length) == NULL)
      return range;
  }
  if (!jh_json_string(value, &text))
    return NULL;
  if (!jh_python_strip(text, &text))
    return invalid;
  if (!text.length)
    return NULL;
  unsigned base = 10;
  size_t scalars = 0;
  for (size_t i = 0; i < text.length;) {
    uint32_t scalar = 0;
    size_t consumed = 0;
    if (jb_utf8_decode(text.data + i, text.length - i, &scalar, &consumed) !=
        HAL_OK)
      return invalid;
    ++scalars;
    if ((scalar >= 'a' && scalar <= 'f') || (scalar >= 'A' && scalar <= 'F'))
      base = 16;
    i += consumed;
  }
  if (scalars == 4 || (text.length >= 2 && text.data[0] == '0' &&
                       (text.data[1] == 'x' || text.data[1] == 'X')))
    base = 16;
  size_t at = 0;
  const bool negative = text.data[0] == '-';
  if (negative || text.data[0] == '+')
    ++at;
  if (base == 16 && text.length - at >= 2 && text.data[at] == '0' &&
      (text.data[at + 1] == 'x' || text.data[at + 1] == 'X')) {
    at += 2;
    if (at < text.length && text.data[at] == '_')
      ++at;
  }
  uint32_t total = 0;
  bool previous_digit = false;
  while (at < text.length) {
    if (text.data[at] == '_') {
      if (!previous_digit)
        return invalid;
      previous_digit = false;
      ++at;
      continue;
    }
    uint32_t scalar = 0;
    size_t consumed = 0;
    if (jb_utf8_decode(text.data + at, text.length - at, &scalar, &consumed) !=
        HAL_OK)
      return invalid;
    const int value_of = digit(h, decimal, scalar);
    if (value_of < 0 || (unsigned)value_of >= base)
      return invalid;
    if (total <= 65535)
      total = total * base + (unsigned)value_of;
    previous_digit = true;
    at += consumed;
  }
  if (!previous_digit)
    return invalid;
  return total > 65535 || (negative && total) ? range : NULL;
}

static bool configured(jb_json_tree_view_t value) {
  switch (jh_json_type(value)) {
  case JB_JSON_NULL:
    return false;
  case JB_JSON_BOOL:
    return jh_hw_bool(value);
  case JB_JSON_ARRAY:
  case JB_JSON_OBJECT:
    return jh_json_count(value) != 0;
  case JB_JSON_STRING: {
    jb_json_span_t text = {0};
    (void)jh_json_string(value, &text);
    return text.length != 0;
  }
  default: {
    jb_json_span_t token = {0};
    double number = 0;
    return jb_json_tree_text(value, &token) != HAL_OK ||
           jh_python_float(token, &number) != HAL_OK || number != 0;
  }
  }
}

/* Runtime's dict(value or {}), including JSON arrays of key/value pairs. */
static jb_json_tree_view_t dictionary(jh_hw_t *h, jb_json_tree_view_t value,
                                      const char *field,
                                      jh_failure_t *failure) {
  if (jh_json_type(value) == JB_JSON_OBJECT || !configured(value))
    return value;
  jb_json_tree_t *object = jh_hw_object(h);
  bool valid = jh_json_type(value) == JB_JSON_ARRAY;
  for (size_t i = 0; valid && i < jh_json_count(value); ++i) {
    const jb_json_tree_view_t pair = jh_json_item(value, i);
    jb_json_tree_view_t key = {0}, item = {0};
    if (jh_json_type(pair) == JB_JSON_STRING) {
      jb_json_span_t text = {0};
      (void)jh_json_string(pair, &text);
      uint32_t scalar = 0;
      size_t first = 0, second = 0;
      valid =
          jb_utf8_decode(text.data, text.length, &scalar, &first) == HAL_OK &&
          jb_utf8_decode(text.data + first, text.length - first, &scalar,
                         &second) == HAL_OK &&
          first + second == text.length;
      if (valid) {
        key = jh_hw_string(h, (jb_json_span_t){text.data, first});
        item = jh_hw_string(h, (jb_json_span_t){text.data + first, second});
      }
    } else {
      valid = (jh_json_type(pair) == JB_JSON_ARRAY ||
               jh_json_type(pair) == JB_JSON_OBJECT) &&
              jh_json_count(pair) == 2;
      if (valid && jh_json_type(pair) == JB_JSON_OBJECT) {
        key = jh_hw_string(h, jh_json_key(jh_json_item(pair, 0)));
        item = jh_hw_string(h, jh_json_key(jh_json_item(pair, 1)));
      } else if (valid) {
        key = jh_json_item(pair, 0);
        item = jh_json_item(pair, 1);
        valid = jh_json_type(key) != JB_JSON_ARRAY &&
                jh_json_type(key) != JB_JSON_OBJECT;
      }
    }
    /* Non-string scalar keys are legal in dict(), but cannot select a JSON
     * manifest field such as usbVid, sourceDir or cache. */
    jb_json_span_t name = {0};
    if (valid && jh_json_string(key, &name) &&
        memchr(name.data, '\0', name.length) == NULL)
      jh_hw_set(h, object, jh_hw_text(h, key), item);
  }
  if (!valid && failure->status == HAL_OK)
    (void)jh_check_fail(failure, JH_PARTS(jh_span("project manifest field '"),
                                          jh_span(field),
                                          jh_span("' must be an object")));
  return jh_hw_root(object);
}

void jh_effective_identity(jh_hw_t *h, jb_json_tree_view_t manifest,
                           jh_failure_t *failure) {
  const jb_json_tree_view_t identity =
      dictionary(h, jh_json_field(manifest, "identity"), "identity", failure);
  decimal_t decimal = {0};
  static const char *const keys[] = {"usbVid", "usbPid"};
  for (size_t i = 0; h->memory.status == HAL_OK && failure->status == HAL_OK &&
                     i < COUNTOF(keys);
       ++i) {
    const char *problem =
        usb_problem(h, &decimal, jh_json_field(identity, keys[i]));
    if (problem != NULL && h->memory.status == HAL_OK)
      (void)jh_check_fail(
          failure, JH_PARTS(jh_span("invalid USB identity configuration: "),
                            jh_span(keys[i]), jh_span(problem)));
  }
  pcre2_match_data_free(decimal.match);
  pcre2_code_free(decimal.code);
}

void jh_effective_sections(jh_hw_t *h, jb_json_tree_view_t manifest,
                           jb_json_tree_view_t settings, const char *target,
                           bool assembly, bool variant, jh_failure_t *failure) {
  const jb_json_tree_view_t overlay =
      jh_json_field(jh_json_field(manifest, "targetProfiles"), target);
  if (assembly) {
    jb_json_tree_view_t build = jh_json_field(manifest, "buildDir");
    if (build.tree == NULL)
      build = jh_json_field(settings, "jaszczurhal.buildDir");
    if (configured(jh_json_field(overlay, "buildDir")))
      build = jh_json_field(overlay, "buildDir");
    if (configured(build) && jh_json_type(build) != JB_JSON_STRING)
      (void)jh_check_fail(
          failure, JH_PARTS(jh_span(
                       "project manifest field 'buildDir' must be a string")));
  }
  if (!variant || failure->status != HAL_OK)
    return;
  jb_json_tree_view_t cmake = jh_json_field(manifest, "cmake");
  const jb_json_tree_view_t profile = jh_json_field(overlay, "cmake");
  if (profile.tree != NULL && jh_json_type(profile) != JB_JSON_OBJECT)
    cmake = profile;
  if (profile.tree == NULL || jh_json_type(profile) != JB_JSON_OBJECT)
    cmake = dictionary(h, cmake, "cmake", failure);
  jb_json_tree_view_t cache = jh_json_field(profile, "cache");
  if (cache.tree == NULL)
    cache = jh_json_field(cmake, "cache");
  const jh_board_descriptor_t *descriptor =
      jh_board_registry_find(&h->registry->boards.targets, jh_span(target));
  const jb_json_tree_view_t provider =
      descriptor != NULL
          ? jh_json_field(jh_json_field(descriptor->root, "build"), "provider")
          : (jb_json_tree_view_t){0};
  /* pin_resolved_target_board replaces a non-object cache for these managed
   * CMake providers before the variant is applied. */
  if (jh_hw_is(provider, "pico-sdk") ||
      jh_hw_is(provider, "jh-stm32-baremetal"))
    return;
  if (failure->status == HAL_OK)
    (void)dictionary(h, cache, "cmake.cache", failure);
}

static const char *field_text(jh_hw_t *h, jb_json_tree_view_t object,
                              const char *key, const char *fallback) {
  const jb_json_tree_view_t value = jh_json_field(object, key);
  return value.tree == NULL                      ? fallback
         : jh_json_type(value) == JB_JSON_STRING ? jh_hw_text(h, value)
                                                 : jh_hw_repr(h, value);
}

static const char *parent_of(jh_hw_t *h, const char *path) {
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  jh_node_storage(&h->memory, jh_path_parent(path, &text, h->failure));
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

static const char *name_of(const char *path) {
  const char *name = path;
  for (const char *p = path; *p; ++p)
    if (*p == '/' || *p == '\\')
      name = p + 1;
  return name;
}

static const char *replace(jh_hw_t *h, const char *text, const char *token,
                           const char *value) {
  jb_string_t out = {0};
  (void)jb_string_init(&out, jb_heap_allocator());
  const size_t length = strlen(token);
  const char *match = NULL;
  while (h->memory.status == HAL_OK && (match = strstr(text, token)) != NULL) {
    jh_put_span(&out, &h->memory.status,
                (jb_json_span_t){text, (size_t)(match - text)});
    jh_put(&out, &h->memory.status, value);
    text = match + length;
  }
  jh_put(&out, &h->memory.status, text);
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&out)));
  jb_string_destroy(&out);
  return result;
}

static const char *expand(jh_hw_t *h, const char *text, const char *directory,
                          jb_json_tree_view_t base, jb_json_tree_view_t overlay,
                          jb_json_tree_view_t settings, const char *target,
                          const char *build_dir) {
  const char *module =
      field_text(h, overlay, "module",
                 field_text(h, base, "module",
                            field_text(h, settings, "jaszczurhal.moduleName",
                                       name_of(directory))));
  const char *project =
      field_text(h, overlay, "project",
                 field_text(h, base, "project",
                            field_text(h, settings, "jaszczurhal.projectName",
                                       name_of(parent_of(h, directory)))));
  const char *const tokens[] = {
      "${jhRoot}", "${project}",     "${projectDir}", "${workspaceFolder}",
      "${module}", "${projectName}", "${buildDir}",   "${target}"};
  const char *const values[] = {jb_string_data(&h->registry->root),
                                directory,
                                directory,
                                directory,
                                module,
                                project,
                                build_dir,
                                target};
  for (size_t i = 0; h->memory.status == HAL_OK && i < COUNTOF(tokens); ++i)
    if (values[i] != NULL)
      text = replace(h, text, tokens[i], values[i]);
  return text;
}

static const char *expand_user(jh_hw_t *h, const char *path) {
  if (*path != '~')
    return path;
  size_t end = 1;
  while (path[end] && path[end] != '/'
#ifdef _WIN32
         && path[end] != '\\'
#endif
  )
    ++end;
  jb_string_t home_dir = {0};
  (void)jb_string_init(&home_dir, jb_heap_allocator());
  bool found = false;
  const char *user =
      jh_hw_join(h, JH_PARTS(((jb_json_span_t){path + 1, end - 1})));
  jh_node_storage(&h->memory,
                  jh_process_user_home(user, &home_dir, &found, h->failure));
  const char *result = found ? jh_hw_join(h, JH_PARTS(jh_string_span(&home_dir),
                                                      jh_span(path + end)))
                             : path;
  jb_string_destroy(&home_dir);
  return result;
}

static const char *resolved_path(jh_hw_t *h, const char *directory,
                                 const char *path) {
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  if (h->memory.status == HAL_OK)
    jh_node_storage(
        &h->memory,
        jh_python_resolve(expand_user(h, path), directory, &text, h->failure));
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

static bool same_path(const char *a, const char *b) {
#ifdef _WIN32
  return _stricmp(a, b) == 0;
#else
  return strcmp(a, b) == 0;
#endif
}

void jh_effective_checkout(jh_hw_t *h, const char *directory,
                           jb_json_tree_view_t manifest,
                           jb_json_tree_view_t settings, const char *target,
                           const char *assembly, jh_failure_t *failure) {
  const char *own = jb_string_data(&h->registry->root);
  const jb_json_tree_view_t overlay =
      jh_json_field(jh_json_field(manifest, "targetProfiles"), target);
  const jb_json_tree_view_t empty = {0};
  const char *build =
      field_text(h, manifest, "buildDir",
                 field_text(h, settings, "jaszczurhal.buildDir",
                            jh_hw_path(h, directory, ".build")));
  build = expand(h, build, directory, manifest, empty, settings, NULL, build);
  if (assembly != NULL)
    build = jh_hw_path(h, jh_hw_path(h, build, target), assembly);
  const char *effective_build = field_text(h, overlay, "buildDir", build);
  effective_build = expand(h, effective_build, directory, manifest, overlay,
                           settings, target, effective_build);
  jb_json_tree_view_t cmake = jh_json_field(manifest, "cmake");
  const jb_json_tree_view_t profile_cmake = jh_json_field(overlay, "cmake");
  const char *source = field_text(h, cmake, "sourceDir", directory);
  source = expand(h, source, directory, manifest, empty, settings, NULL, build);
  if (profile_cmake.tree != NULL) {
    source = field_text(
        h, profile_cmake, "sourceDir",
        jh_json_type(profile_cmake) == JB_JSON_OBJECT ? source : directory);
  }
  source = expand(h, source, directory, manifest, overlay, settings, target,
                  effective_build);
  source = resolved_path(h, directory, *source ? source : directory);
  const jb_json_tree_view_t configured =
      jh_json_field(settings, "jaszczurhal.root");
  const char *root = jh_json_type(configured) == JB_JSON_STRING
                         ? jh_hw_text(h, configured)
                         : "";
  jb_string_t messages = {0};
  (void)jb_string_init(&messages, jb_heap_allocator());
  if (*root) {
    root = resolved_path(h, directory, root);
    if (!same_path(root, own)) {
      const char *message = jh_hw_join(
          h,
          JH_PARTS(
              jh_span(
                  "[JH-HAL-ROOT] .vscode/settings.json: jaszczurhal.root is "),
              jh_span(root), jh_span(", but this jh-vscode runs from "),
              jh_span(own), jh_span("; point it at "), jh_span(own),
              jh_span(" or run the jh-vscode of that checkout")));
      jh_node_storage(&h->memory, jh_append(&messages, message));
    }
  }
  const char *source_parent = parent_of(h, source),
             *source_root = parent_of(h, source_parent);
  if (strcmp(name_of(source), "jh_firmware_project") == 0 &&
      strcmp(name_of(source_parent), "cmake") == 0 &&
      !same_path(source_root, own)) {
    if (jb_string_size(&messages))
      jh_node_storage(&h->memory, jh_append(&messages, "\n"));
    const char *origin =
        cmake.tree != NULL
            ? ".vscode/jaszczurhal.project.json"
            : jh_hw_join(h, JH_PARTS(jh_span("registry/targetProfiles:"),
                                     jh_span(target)));
    const char *message = jh_hw_join(
        h, JH_PARTS(jh_span("[JH-HAL-ROOT] "), jh_span(origin),
                    jh_span(": cmake.sourceDir is the dispatcher of "),
                    jh_span(source_root),
                    jh_span(", but this jh-vscode runs from "), jh_span(own),
                    jh_span("; use ${jhRoot}/cmake/jh_firmware_project")));
    jh_node_storage(&h->memory, jh_append(&messages, message));
  }
  if (h->memory.status == HAL_OK && jb_string_size(&messages))
    (void)jh_check_fail(failure, JH_PARTS(jh_string_span(&messages)));
  jb_string_destroy(&messages);
}

#include "jh_feature_lint.h"
#include "jh_feature_registry.h"
#include "jh_json_file.h"
#include <jb/array.h>
#include <jb/filesystem.h>
#include <string.h>

/* ---- Inputs ---- */

static const char *const ignored_directories[] = {".build", ".git",
                                                  "third_party"};

static bool lint_input(const char *name) {
  return strcmp(name, "hal_project_config.h") == 0 ||
         strcmp(name, "jaszczurhal.project.json") == 0;
}

/* A directory os.walk would leave out: an ignored name, a build* name, or
 * tests/hardware without include_hardware. */
static bool skipped_directory(const char *parent_name, const char *name,
                              bool include_hardware) {
  for (size_t i = 0; i < COUNTOF(ignored_directories); ++i)
    if (strcmp(name, ignored_directories[i]) == 0)
      return true;
  if (strncmp(name, "build", 5) == 0)
    return true;
  return !include_hardware && strcmp(parent_name, "tests") == 0 &&
         strcmp(name, "hardware") == 0;
}

/* A path below directory, joined with native separators. */
static hal_status_t joined(const char *directory, const char *name,
                           jb_string_t *out) {
  size_t required = 0;
  hal_status_t status = jb_fs_path_join(directory, name, NULL, 0, &required);
  if (status != HAL_OK && status != HAL_EOVERFLOW)
    return status;
  status = jb_string_resize(out, required, '\0');
  if (status == HAL_OK)
    status = jb_fs_path_join(directory, name, jb_string_mutable_data(out),
                             required, &required);
  return status == HAL_OK ? jb_string_resize(out, required - 1, '\0') : status;
}

typedef struct {
  const char *directory;
  jb_vector_t *files, *directories;
} listing_t;

/* One entry of a listed directory: a directory, or os.walk's file. */
static hal_status_t list_entry(void *context, const char *name,
                               const jb_fs_info_t *info) {
  const listing_t *listing = context;
  bool directory = info->kind == JB_FS_DIRECTORY;
  bool link = info->kind == JB_FS_LINK;
  if (link) {
    jb_string_t target = {0};
    hal_status_t status = jb_string_init(&target, jb_heap_allocator());
    if (status == HAL_OK)
      status = joined(listing->directory, name, &target);
    jb_fs_info_t followed = {0};
    if (status == HAL_OK &&
        jb_fs_status(jb_string_data(&target), true, &followed) == HAL_OK)
      directory = followed.kind == JB_FS_DIRECTORY;
    jb_string_destroy(&target);
    if (status != HAL_OK)
      return status;
  }
  // A link to a directory is listed and never entered.
  if (directory && link)
    return HAL_OK;
  return jh_list_push(directory ? listing->directories : listing->files,
                      jh_span(name));
}

/* os.walk(root) without following links: the inputs of every directory. */
static hal_status_t walk(const char *root, bool include_hardware,
                         jb_vector_t *inputs) {
  jb_vector_t pending = {0}, files = {0}, directories = {0};
  jb_string_t path = {0}, parent_name = {0};
  hal_status_t status = jh_list_init(&pending);
  if (status == HAL_OK)
    status = jh_list_init(&files);
  if (status == HAL_OK)
    status = jh_list_init(&directories);
  if (status == HAL_OK)
    status = jb_string_init(&path, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_string_init(&parent_name, jb_heap_allocator());
  if (status == HAL_OK)
    status = jh_list_push(&pending, jh_span(root));
  while (status == HAL_OK && jb_vector_size(&pending) != 0) {
    const size_t last = jb_vector_size(&pending) - 1;
    jb_string_t directory = *(jb_string_t *)jb_vector_at(&pending, last);
    (void)jb_vector_resize(&pending, last);
    jh_list_destroy(&files);
    jh_list_destroy(&directories);
    status = jh_list_init(&files);
    if (status == HAL_OK)
      status = jh_list_init(&directories);
    listing_t listing = {jb_string_data(&directory), &files, &directories};
    if (status == HAL_OK) {
      // A directory that cannot be listed is skipped, as os.walk does.
      const hal_status_t listed =
          jb_fs_list(jb_string_data(&directory), list_entry, &listing);
      if (listed == HAL_ENOMEM || listed == HAL_EOVERFLOW)
        status = listed;
    }
    size_t required = 0;
    if (status == HAL_OK) {
      const hal_status_t named =
          jb_fs_path_filename(jb_string_data(&directory), NULL, 0, &required);
      if (named == HAL_OK || named == HAL_EOVERFLOW)
        status = jb_string_resize(&parent_name, required, '\0');
      else
        status = named;
    }
    if (status == HAL_OK)
      status = jb_fs_path_filename(jb_string_data(&directory),
                                   jb_string_mutable_data(&parent_name),
                                   required, &required);
    for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&files); ++i) {
      const char *name = jb_string_data(jh_list_at(&files, i));
      if (!lint_input(name))
        continue;
      status = joined(jb_string_data(&directory), name, &path);
      if (status == HAL_OK)
        status = jh_list_push(inputs, jh_string_span(&path));
    }
    for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&directories);
         ++i) {
      const char *name = jb_string_data(jh_list_at(&directories, i));
      if (skipped_directory(jb_string_data(&parent_name), name,
                            include_hardware))
        continue;
      status = joined(jb_string_data(&directory), name, &path);
      if (status == HAL_OK)
        status = jh_list_push(&pending, jh_string_span(&path));
    }
    jb_string_destroy(&directory);
  }
  jb_string_destroy(&parent_name);
  jb_string_destroy(&path);
  jh_list_destroy(&directories);
  jh_list_destroy(&files);
  jh_list_destroy(&pending);
  return status;
}

hal_status_t jh_feature_lint_inputs(const char *root, bool include_hardware,
                                    jb_vector_t *inputs,
                                    jh_failure_t *failure) {
  if (root == NULL || inputs == NULL)
    return jh_fail_arguments(failure);
  jb_string_t top = {0};
  hal_status_t status = jb_string_init(&top, jb_heap_allocator());
  if (status == HAL_OK)
    status = jh_resolved_path(root, &top);
  jb_fs_info_t info = {0};
  const bool found = status == HAL_OK &&
                     jb_fs_status(jb_string_data(&top), true, &info) == HAL_OK;
  if (found && info.kind == JB_FS_REGULAR) {
    status = jh_list_push(inputs, jh_string_span(&top));
  } else if (found && info.kind == JB_FS_DIRECTORY) {
    status = walk(jb_string_data(&top), include_hardware, inputs);
  } else if (status == HAL_OK) {
    const jb_json_span_t parts[] = {
        jh_string_span(&top), jh_span(": lint input root does not exist")};
    status = jh_fail(failure, HAL_ENOENT, 2, parts, COUNTOF(parts));
    jb_string_destroy(&top);
    return status;
  }
  status = jh_fail_storage(failure, status);
  jb_string_destroy(&top);
  return status;
}

/* ---- Manifests ---- */

/* Fields a manifest no longer has: the header declares targets and variants. */
static const char *const removed_fields[] = {"variants", "example"};
/* CMake cache entries that would pass definitions or pick sources. */
static const char *const configuration_keys[] = {
    "EXTRA_HAL_DEFINES", "JH_EXTRA_DEFINES", "JH_PROJECT_SOURCES"};

/* The findings of one cmake.cache object at location. */
static hal_status_t cache_findings(jb_json_tree_view_t cmake,
                                   jb_json_span_t label,
                                   jb_json_span_t location,
                                   jb_vector_t *findings) {
  jb_json_tree_view_t cache = {0};
  if (!jh_json_member(cmake, "cache", &cache) ||
      jh_json_type(cache) != JB_JSON_OBJECT)
    return HAL_OK;
  size_t count = 0;
  (void)jb_json_tree_count(cache, &count);
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < count; ++i) {
    jb_json_tree_view_t entry = {0};
    jb_json_span_t key = {NULL, 0};
    status = jb_json_tree_at(cache, i, &entry);
    if (status == HAL_OK)
      status = jb_json_tree_key(entry, &key);
    if (status != HAL_OK)
      break;
    bool configuration = jh_feature_symbol(key, false);
    for (size_t k = 0; k < COUNTOF(configuration_keys); ++k)
      configuration =
          configuration || jh_span_equal(key, jh_span(configuration_keys[k]));
    const bool variant = jh_span_equal(key, jh_span("JH_VARIANT"));
    if (!variant && !configuration)
      continue;
    const jb_json_span_t parts[] = {
        label,
        jh_span(": "),
        location,
        jh_span("."),
        key,
        jh_span(variant ? ": [JH-CFG-MANIFEST] the manifest does not pick a "
                          "variant; pass --variant"
                        : ": [JH-CFG-MANIFEST] project configuration belongs "
                          "in hal_project_config.h")};
    status = jh_list_push_parts(findings, parts, COUNTOF(parts));
  }
  return status;
}

hal_status_t jh_manifest_findings(jb_json_tree_view_t manifest,
                                  jb_json_span_t label, jb_vector_t *findings,
                                  jh_failure_t *failure) {
  if (findings == NULL)
    return jh_fail_arguments(failure);
  hal_status_t status = HAL_OK;
  jb_json_tree_view_t value = {0};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(removed_fields); ++i) {
    if (!jh_json_member(manifest, removed_fields[i], &value))
      continue;
    const jb_json_span_t parts[] = {
        label, jh_span(": [JH-CFG-MANIFEST] '"), jh_span(removed_fields[i]),
        jh_span("' is not a manifest field; declare targets with "
                "JH_PROJECT_TARGETS(X) and variants with "
                "JH_PROJECT_VARIANTS(X) in hal_project_config.h")};
    status = jh_list_push_parts(findings, parts, COUNTOF(parts));
  }
  if (status == HAL_OK && jh_json_member(manifest, "cmake", &value))
    status = cache_findings(value, label, jh_span("cmake.cache"), findings);
  jb_json_tree_view_t profiles = {0};
  size_t count = 0;
  if (status == HAL_OK &&
      jh_json_member(manifest, "targetProfiles", &profiles) &&
      jh_json_type(profiles) == JB_JSON_OBJECT)
    (void)jb_json_tree_count(profiles, &count);
  jb_string_t location = {0};
  if (status == HAL_OK)
    status = jb_string_init(&location, jb_heap_allocator());
  for (size_t i = 0; status == HAL_OK && i < count; ++i) {
    jb_json_tree_view_t profile = {0}, cmake = {0};
    jb_json_span_t target = {NULL, 0};
    status = jb_json_tree_at(profiles, i, &profile);
    if (status == HAL_OK)
      status = jb_json_tree_key(profile, &target);
    if (status != HAL_OK)
      break;
    if (!jh_json_member(profile, "cmake", &cmake))
      continue;
    jb_string_clear(&location);
    status = jb_string_append(&location, "targetProfiles.", 15);
    if (status == HAL_OK && target.length != 0)
      status = jb_string_append(&location, target.data, target.length);
    if (status == HAL_OK)
      status = jb_string_append(&location, ".cmake.cache", 12);
    if (status == HAL_OK)
      status =
          cache_findings(cmake, label, jh_string_span(&location), findings);
  }
  jb_string_destroy(&location);
  return jh_fail_storage(failure, status);
}

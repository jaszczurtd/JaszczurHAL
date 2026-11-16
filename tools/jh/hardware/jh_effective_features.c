#include "jh_effective_features.h"
#include "jh_effective_project.h"
#include "jh_feature_lint.h"
#include "jh_hardware_internal.h"
#include "jh_json_digest.h"
#include "jh_process_support.h"
#include "jh_project_config.h"
#include <jb/filesystem.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  jh_hw_t *h;
  const jh_feature_registry_t *features;
  const jb_vector_t *roots;
  jb_vector_t *findings;
  jb_vector_t records; /* Completed, owned-by-h record views. */
  jb_temp_dir_t *scratch;
} batch_t;

static const char *parent(jh_hw_t *h, const char *path) {
  jb_string_t result = {0};
  (void)jb_string_init(&result, jb_heap_allocator());
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jh_path_parent(path, &result, h->failure));
  const char *text = jh_hw_join(h, JH_PARTS(jh_string_span(&result)));
  jb_string_destroy(&result);
  return text;
}

static const char *basename_of(const char *path) {
  const char *name = path;
  for (const char *p = path; *p; ++p)
    if (*p == '/' || *p == '\\')
      name = p + 1;
  return name;
}

static const char *absolute(jh_hw_t *h, const char *path) {
  jb_string_t result = {0};
  (void)jb_string_init(&result, jb_heap_allocator());
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jh_python_resolve(path, NULL, &result, h->failure));
  const char *text = jh_hw_join(h, JH_PARTS(jh_string_span(&result)));
  jb_string_destroy(&result);
  return text;
}

static const char *posix(jh_hw_t *h, const char *path) {
  jb_string_t result = {0};
  (void)jb_string_init(&result, jb_heap_allocator());
  jh_node_storage(&h->memory, jh_append_posix(&result, jh_span(path)));
  const char *text = jh_hw_join(h, JH_PARTS(jh_string_span(&result)));
  jb_string_destroy(&result);
  return text;
}

static const char *display(batch_t *b, const char *path) {
  jh_hw_t *h = b->h;
  const char *full = posix(h, absolute(h, path)), *best = NULL;
  size_t best_parts = SIZE_MAX;
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(b->roots);
       ++i) {
    const char *base = absolute(h, jb_string_data(jh_list_at(b->roots, i)));
    jb_fs_info_t info = {0};
    const hal_status_t status = jb_fs_status(base, true, &info);
    if (status == HAL_ENOMEM || status == HAL_EOVERFLOW)
      jh_node_storage(&h->memory, status);
    if (status != HAL_OK || info.kind != JB_FS_DIRECTORY)
      base = parent(h, base);
    base = posix(h, base);
    const size_t length = strlen(base);
    if (h->memory.status != HAL_OK || length == 0)
      break;
    if (strlen(full) < length)
      continue;
#ifdef _WIN32
    const bool matches = _strnicmp(full, base, length) == 0;
#else
    const bool matches = strncmp(full, base, length) == 0;
#endif
    if (!matches || (base[length - 1] != '/' && full[length] != '\0' &&
                     full[length] != '/'))
      continue;
    const char *relative = full + length;
    if (*relative == '/')
      ++relative;
    size_t parts = *relative ? 1u : 0u;
    for (const char *p = relative; *p; ++p)
      if (*p == '/')
        ++parts;
    if (best == NULL || parts < best_parts ||
        (parts == best_parts && strcmp(relative, best) < 0)) {
      best = *relative ? relative : ".";
      best_parts = parts;
    }
  }
  return best != NULL ? best : full;
}

static const char *display_source(batch_t *b, const jb_string_t *source) {
  jh_hw_t *h = b->h;
  const char *text = jb_string_data(source), *colon = strrchr(text, ':');
  if (colon == NULL)
    return text;
  const char *location =
      jh_hw_join(h, JH_PARTS(((jb_json_span_t){text, (size_t)(colon - text)})));
  bool is_absolute = false;
  if (jb_fs_path_is_absolute(location, &is_absolute) != HAL_OK || !is_absolute)
    return text;
  return jh_hw_join(h, JH_PARTS(jh_span(display(b, location)), jh_span(colon)));
}

static jb_json_tree_view_t strings(jh_hw_t *h, const jb_vector_t *values) {
  jb_json_tree_t *result = jh_hw_array(h);
  for (size_t i = 0; i < jb_vector_size(values); ++i)
    jh_hw_append(h, result,
                 jh_hw_string(h, jh_string_span(jh_list_at(values, i))));
  return jh_hw_root(result);
}

static void nullable(jh_hw_t *h, jb_json_tree_t *object, const char *key,
                     const char *value) {
  jh_hw_set(h, object, key,
            value != NULL && *value
                ? jh_hw_string(h, jh_span(value))
                : jh_hw_root(jh_hw_parse_tree(h, jh_span("null"))));
}

static void append_finding(batch_t *b, jb_json_span_t text) {
  if (!jh_list_contains(b->findings, text))
    jh_node_storage(&b->h->memory, jh_list_push(b->findings, text));
}

static void finding(batch_t *b, const char *project, const char *label,
                    jh_failure_t *failure) {
  jh_hw_t *h = b->h;
  if (failure->status == HAL_ENOMEM || failure->status == HAL_EOVERFLOW) {
    jh_failure_destroy(h->failure);
    *h->failure = *failure;
    jh_failure_init(failure);
    h->memory.status = h->failure->status;
    return;
  }
  append_finding(
      b, jh_span(jh_hw_join(h, JH_PARTS(jh_span(display(b, project)),
                                        jh_span(label != NULL ? " [" : ""),
                                        jh_span(label != NULL ? label : ""),
                                        jh_span(label != NULL ? "]" : ""),
                                        jh_span(": [JH-CFG-EFFECTIVE] "),
                                        jh_string_span(&failure->message)))));
}

static const char *label(jh_hw_t *h, const char *target, const char *assembly,
                         const char *variant) {
  return jh_hw_join(
      h, JH_PARTS(jh_span("target="), jh_span(*target ? target : "default"),
                  jh_span(assembly != NULL ? ", assembly=" : ""),
                  jh_span(assembly != NULL ? assembly : ""),
                  jh_span(", variant="), jh_span(*variant ? variant : "base")));
}

static void record(batch_t *b, const char *project, const char *target,
                   const char *board, const char *assembly, const char *variant,
                   const jh_project_build_t *build,
                   const jb_vector_t *required) {
  jh_hw_t *h = b->h;
  jb_vector_t required_spans = {0};
  jh_node_storage(&h->memory,
                  jb_vector_init(&required_spans, sizeof(jb_json_span_t),
                                 jb_heap_allocator()));
  for (size_t i = 0; i < jb_vector_size(required); ++i) {
    const jb_json_span_t span = jh_string_span(jh_list_at(required, i));
    jh_node_storage(&h->memory, jb_vector_append(&required_spans, &span, 1));
  }
  jh_feature_resolution_t resolution;
  jh_feature_resolution_init(&resolution);
  const jh_feature_target_t selected = {jh_span(target),
                                        jb_vector_const_data(&required_spans),
                                        jb_vector_size(&required_spans)};
  const char *context = jh_hw_join(
      h, JH_PARTS(jh_span(jh_hw_path(h, project, "hal_project_config.h")),
                  jh_span(" ["), jh_span(label(h, target, assembly, variant)),
                  jh_span("]")));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jh_feature_resolve(
                        b->features, jb_vector_const_data(&build->requests),
                        jb_vector_size(&build->requests), jh_span(context),
                        *target ? &selected : NULL, &resolution, h->failure));
  for (size_t i = 0; i < jb_vector_size(&resolution.findings); ++i)
    append_finding(b, jh_string_span(jh_list_at(&resolution.findings, i)));
  jb_json_tree_t *result = jh_hw_object(h), *provenance = jh_hw_object(h);
  jh_hw_set(h, result, "project",
            jh_hw_string(h, jh_span(display(b, project))));
  nullable(h, result, "target", target);
  nullable(h, result, "board", board);
  nullable(h, result, "variant", variant);
  if (assembly != NULL)
    jh_hw_set(h, result, "assembly", jh_hw_string(h, jh_span(assembly)));
  jh_hw_set(h, result, "requestedFeatures", strings(h, &resolution.requested));
  jh_hw_set(h, result, "resolvedFeatures", strings(h, &resolution.resolved));
  jh_hw_set(h, result, "resolvedFeaturesDigest",
            jh_hw_string(h, jh_span(resolution.digest)));
  for (size_t i = 0; i < jb_vector_size(&resolution.provenance); ++i) {
    const jh_feature_provenance_t *item =
        jb_vector_const_at(&resolution.provenance, i);
    jb_json_tree_t *sources = jh_hw_array(h);
    for (size_t j = 0; j < jb_vector_size(&item->sources); ++j)
      jh_hw_append(h, sources,
                   jh_hw_string(h, jh_span(display_source(
                                       b, jh_list_at(&item->sources, j)))));
    jh_hw_set(h, provenance, jb_string_data(&item->symbol),
              jh_hw_root(sources));
  }
  jh_hw_set(h, result, "provenance", jh_hw_root(provenance));
  const jb_json_tree_view_t view = jh_hw_root(result);
  jh_node_storage(&h->memory, jb_vector_append(&b->records, &view, 1));
  jh_feature_resolution_destroy(&resolution);
  jb_vector_destroy(&required_spans);
}

static int compare_records(const void *left, const void *right) {
  const jb_json_tree_view_t a = *(const jb_json_tree_view_t *)left,
                            b = *(const jb_json_tree_view_t *)right;
  static const char *const keys[] = {"project", "target", "board", "assembly",
                                     "variant"};
  for (size_t i = 0; i < COUNTOF(keys); ++i) {
    jb_json_span_t x = {"", 0}, y = {"", 0};
    (void)jh_json_string(jh_json_field(a, keys[i]), &x);
    (void)jh_json_string(jh_json_field(b, keys[i]), &y);
    const int order = jh_span_compare(x, y);
    if (order != 0)
      return order;
  }
  return 0;
}

static void report(batch_t *b, jb_string_t *out) {
  jh_hw_t *h = b->h;
  if (jb_vector_size(&b->records) > 1)
    qsort(jb_vector_data(&b->records), jb_vector_size(&b->records),
          sizeof(jb_json_tree_view_t), compare_records);
  jb_json_tree_t *rows = jh_hw_array(h), *contract = jh_hw_array(h),
                 *result = jh_hw_object(h);
  static const char *const keys[] = {
      "project",           "target",          "board", "assembly", "variant",
      "requestedFeatures", "resolvedFeatures"};
  for (size_t i = 0; i < jb_vector_size(&b->records); ++i) {
    const jb_json_tree_view_t row =
        *(const jb_json_tree_view_t *)jb_vector_const_at(&b->records, i);
    jh_hw_append(h, rows, row);
    jb_json_tree_t *entry = jh_hw_object(h);
    for (size_t j = 0; j < COUNTOF(keys); ++j) {
      const jb_json_tree_view_t value = jh_json_field(row, keys[j]);
      if (value.tree != NULL)
        jh_hw_set(h, entry, keys[j], value);
    }
    jh_hw_append(h, contract, jh_hw_root(entry));
  }
  char digest[65] = {0};
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jh_json_digest(jh_hw_root(contract), true, digest));
  jh_hw_set(h, result, "schemaVersion", jh_hw_integer(h, 1));
  jh_hw_set(h, result, "registrySchemaVersion", jh_hw_integer(h, 1));
  jh_hw_set(h, result, "registryDigest",
            jh_hw_string(h, jh_span(b->features->digest)));
  jh_hw_set(h, result, "registrySchemaDigest",
            jh_hw_string(h, jh_span(b->features->schema_digest)));
  jh_hw_set(h, result, "matrixDigest", jh_hw_string(h, jh_span(digest)));
  jh_hw_set(h, result, "configurations", jh_hw_root(rows));
  if (h->memory.status == HAL_OK) {
    const jb_json_tree_format_t options = {64u * 1024u * 1024u, 2, true, true};
    size_t written = 0;
    jh_node_storage(&h->memory,
                    jb_json_tree_format(jh_hw_root(result), &options,
                                        jh_string_sink, out, &written));
  }
  jh_node_storage(&h->memory, jh_append(out, "\n"));
}

static bool contains_target(const jh_board_descriptor_t *board,
                            const char *target) {
  const jb_json_tree_view_t values =
      jh_json_field(board->root, "compatibleTargets");
  for (size_t i = 0; i < jh_json_count(values); ++i)
    if (jh_hw_is(jh_json_item(values, i), target))
      return true;
  return false;
}

static const char *board_candidate(jh_hw_t *h, const char *target,
                                   const char *name, const char *source,
                                   bool scoped, jh_failure_t *failure) {
  if (name == NULL)
    return NULL;
  const jh_board_registry_t *registry = &h->registry->boards;
  const jh_board_descriptor_t *descriptor =
      jh_board_registry_find(&registry->targets, jh_span(target));
  if (descriptor == NULL)
    return name;
  const jh_board_descriptor_t *board =
      jh_board_registry_find(&registry->boards, jh_span(name));
  if (board != NULL && contains_target(board, target))
    return name;
  jb_string_t known = {0}, owners = {0};
  (void)jb_string_init(&known, jb_heap_allocator());
  (void)jb_string_init(&owners, jb_heap_allocator());
  for (size_t i = 0; i < jb_vector_size(&registry->boards); ++i) {
    const jh_board_descriptor_t *item =
        jb_vector_const_at(&registry->boards, i);
    if (contains_target(item, target)) {
      if (jb_string_size(&known))
        jh_put(&known, &h->memory.status, ", ");
      jh_put_span(&known, &h->memory.status, jh_string_span(&item->id));
    }
  }
  for (size_t i = 0; board != NULL && i < jb_vector_size(&registry->targets);
       ++i) {
    const jh_board_descriptor_t *item =
        jb_vector_const_at(&registry->targets, i);
    if (contains_target(board, jb_string_data(&item->id))) {
      if (jb_string_size(&owners))
        jh_put(&owners, &h->memory.status, ", ");
      jh_put_span(&owners, &h->memory.status, jh_string_span(&item->id));
    }
  }
  const bool unrestricted = jb_string_size(&known) == 0;
  if (h->memory.status == HAL_OK && !unrestricted &&
      !(board != NULL && !scoped))
    (void)jh_check_fail(
        failure,
        JH_PARTS(jh_span("[JH-CFG-BOARD] "), jh_span(source),
                 jh_span(": board '"), jh_span(name),
                 jh_span(board == NULL ? "' is not in the board registry; "
                                       : "' belongs to "),
                 board == NULL ? jh_span("") : jh_string_span(&owners),
                 jh_span(board == NULL ? "" : ", not to target "),
                 jh_span(board == NULL ? "" : target),
                 jh_span(board == NULL ? "" : "; "), jh_span(target),
                 jh_span(" boards: "), jh_string_span(&known)));
  jb_string_destroy(&owners);
  jb_string_destroy(&known);
  return unrestricted ? name : NULL;
}

static const char *optional_text(jh_hw_t *h, jb_json_tree_view_t value) {
  if (value.tree == NULL || jh_json_type(value) == JB_JSON_NULL)
    return NULL;
  return jh_json_type(value) == JB_JSON_STRING ? jh_hw_text(h, value)
                                               : jh_hw_repr(h, value);
}

static const char *target_text(jh_hw_t *h, jb_json_tree_view_t value) {
  const jb_json_type_t type = jh_json_type(value);
  if (value.tree == NULL || type == JB_JSON_NULL ||
      (type == JB_JSON_BOOL && !jh_hw_bool(value)) ||
      ((type == JB_JSON_ARRAY || type == JB_JSON_OBJECT) &&
       jh_json_count(value) == 0))
    return "";
  if (type == JB_JSON_NUMBER) {
    jb_json_span_t token = {0};
    double number = 0;
    jh_node_storage(&h->memory, jb_json_tree_text(value, &token));
    if (h->memory.status == HAL_OK)
      jh_node_storage(&h->memory, jh_python_float(token, &number));
    if (number == 0)
      return "";
  }
  return optional_text(h, value);
}

static const char *manifest_board(jh_hw_t *h, jb_json_tree_view_t manifest,
                                  const char *target,
                                  const char *assembly_board,
                                  jh_failure_t *failure) {
  static const char source[] = ".vscode/jaszczurhal.project.json";
  const char *base_target = target_text(h, jh_json_field(manifest, "target"));
  if (!*base_target)
    base_target = "rp2040";
  const char *base = optional_text(h, jh_json_field(manifest, "board"));
  (void)board_candidate(h, base_target, base, source, true, failure);
  const jb_json_tree_view_t profiles =
      jh_json_field(manifest, "targetProfiles");
  for (size_t i = 0; failure->status == HAL_OK && i < jh_json_count(profiles);
       ++i) {
    const jb_json_tree_view_t profile = jh_json_item(profiles, i);
    const char *name = jh_hw_key(h, profile);
    (void)board_candidate(
        h, name, optional_text(h, jh_json_field(profile, "board")),
        jh_hw_join(h, JH_PARTS(jh_span(source), jh_span(" targetProfiles."),
                               jh_span(name))),
        true, failure);
  }
  if (failure->status != HAL_OK)
    return NULL;
  if (assembly_board != NULL)
    return board_candidate(h, target, assembly_board, "cli", true, failure);
  const char *chosen = board_candidate(
      h, target,
      optional_text(h, jh_json_field(jh_json_field(profiles, target), "board")),
      jh_hw_join(h, JH_PARTS(jh_span(source), jh_span(" targetProfiles."),
                             jh_span(target))),
      true, failure);
  if (chosen == NULL && failure->status == HAL_OK)
    chosen = board_candidate(h, target, base, source,
                             strcmp(target, base_target) == 0, failure);
  if (chosen == NULL && failure->status == HAL_OK) {
    const jh_board_descriptor_t *descriptor =
        jh_board_registry_find(&h->registry->boards.targets, jh_span(target));
    if (descriptor != NULL)
      chosen =
          optional_text(h, jh_json_field(descriptor->root, "defaultBoard"));
  }
  return chosen;
}

static void assembly_selectors(jh_hw_t *h, jb_json_tree_view_t manifest,
                               jb_vector_t *messages) {
  static const char label_text[] =
      "[JH-HW-BOARD] .vscode/jaszczurhal.project.json: ";
  static const char hint[] =
      ": device_tree.json selects target and board through its assemblies";
  static const char store[] = "; store the assembly key as 'assembly'";
  static const char *const selectors[] = {"target", "board"};
  for (size_t i = 0; i < COUNTOF(selectors); ++i)
    if (optional_text(h, jh_json_field(manifest, selectors[i])) != NULL)
      jh_node_storage(
          &h->memory,
          jh_list_push_parts(messages,
                             JH_PARTS(jh_span(label_text), jh_span("'"),
                                      jh_span(selectors[i]), jh_span("'"),
                                      jh_span(hint), jh_span(store))));
  const jb_json_tree_view_t profiles =
      jh_json_field(manifest, "targetProfiles");
  for (size_t i = 0; i < jh_json_count(profiles); ++i) {
    const jb_json_tree_view_t profile = jh_json_item(profiles, i);
    if (jh_json_field(profile, "board").tree != NULL)
      jh_node_storage(
          &h->memory,
          jh_list_push_parts(messages,
                             JH_PARTS(jh_span(label_text),
                                      jh_span("targetProfiles."),
                                      jh_span(jh_hw_key(h, profile)),
                                      jh_span(".board"), jh_span(hint))));
  }
  for (size_t i = 0; i <= jh_json_count(profiles); ++i) {
    const jb_json_tree_view_t item =
        i == 0 ? manifest : jh_json_item(profiles, i - 1);
    const jb_json_tree_view_t cache =
        jh_json_field(jh_json_field(item, "cmake"), "cache");
    const char *prefix =
        i == 0 ? ""
               : jh_hw_join(h, JH_PARTS(jh_span("targetProfiles."),
                                        jh_span(jh_hw_key(h, item)),
                                        jh_span(".")));
    static const char *const keys[] = {"JH_BOARD", "JH_ASSEMBLY"};
    for (size_t j = 0; j < COUNTOF(keys); ++j)
      if (jh_json_field(cache, keys[j]).tree != NULL)
        jh_node_storage(
            &h->memory,
            jh_list_push_parts(
                messages, JH_PARTS(jh_span(label_text), jh_span(prefix),
                                   jh_span("cmake.cache."), jh_span(keys[j]),
                                   jh_span(hint), jh_span(store))));
  }
}

static void manifest_check(jh_hw_t *h, jb_json_tree_view_t manifest,
                           bool assembly, const char *tree_path,
                           jh_failure_t *failure) {
  jb_vector_t messages = {0};
  jh_node_storage(&h->memory, jh_list_init(&messages));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jh_manifest_findings(
                        manifest, jh_span(".vscode/jaszczurhal.project.json"),
                        &messages, h->failure));
  if (jb_vector_size(&messages) == 0)
    jh_effective_identity(h, manifest, failure);
  if (jb_vector_size(&messages) == 0 && assembly && failure->status == HAL_OK)
    assembly_selectors(h, manifest, &messages);
  if (jb_vector_size(&messages) != 0) {
    jb_string_t joined = {0};
    (void)jb_string_init(&joined, jb_heap_allocator());
    for (size_t i = 0; i < jb_vector_size(&messages); ++i) {
      if (i)
        jh_put(&joined, &h->memory.status, "\n");
      jh_put_span(&joined, &h->memory.status,
                  jh_string_span(jh_list_at(&messages, i)));
    }
    if (h->memory.status == HAL_OK)
      (void)jh_check_fail(failure, JH_PARTS(jh_string_span(&joined)));
    jb_string_destroy(&joined);
  } else if (!assembly && failure->status == HAL_OK) {
    const char *selected =
        optional_text(h, jh_json_field(manifest, "assembly"));
    if (selected != NULL && *selected)
      (void)jh_check_fail(
          failure,
          JH_PARTS(jh_span("[JH-HW-ASSEMBLY] .vscode/jaszczurhal.project.json: "
                           "assembly "),
                   jh_span(jh_hw_repr(h, jh_json_field(manifest, "assembly"))),
                   jh_span(" needs "), jh_span(tree_path)));
  }
  jh_list_destroy(&messages);
}

typedef struct {
  jh_project_build_t build;
  const char *target, *board, *assembly, *variant;
  jb_vector_t required;
} evaluated_t;

static void evaluated_destroy(evaluated_t *item) {
  if (item == NULL)
    return;
  jh_project_build_destroy(&item->build);
  jh_list_destroy(&item->required);
  free(item);
}

static hal_status_t evaluate(batch_t *b, const char *project,
                             const char *target, const char *board,
                             const char *assembly, const char *variant,
                             const char *preinclude, bool skip_rejected,
                             jb_vector_t *builds, jh_failure_t *failure) {
  jh_hw_t *h = b->h;
  const jh_board_descriptor_t *descriptor =
      jh_board_registry_find(&h->registry->boards.targets, jh_span(target));
  if (*target && descriptor == NULL)
    return jh_check_fail(
        failure,
        JH_PARTS(jh_span("[JH-CFG-TARGET] unknown target "),
                 jh_span(jh_hw_repr(h, jh_hw_string(h, jh_span(target))))));
  evaluated_t *item = calloc(1, sizeof *item);
  if (item == NULL)
    return jh_fail_storage(failure, HAL_ENOMEM);
  jh_project_build_init(&item->build);
  (void)jh_list_init(&item->required);
  item->target = target;
  item->board = board;
  item->assembly = assembly;
  item->variant = variant;
  const jb_json_tree_view_t facts =
      descriptor != NULL ? descriptor->root : (jb_json_tree_view_t){0};
  const jb_json_tree_view_t required = jh_json_field(facts, "requiredFeatures");
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(required); ++i)
    status = jh_list_push(&item->required,
                          jh_span(jh_hw_text(h, jh_json_item(required, i))));
  jb_vector_t empty = {0};
  (void)jh_list_init(&empty);
  const jh_project_build_options_t options = {
      jh_hw_path(h, project, "hal_project_config.h"),
      preinclude,
      target,
      jh_hw_text(h,
                 jh_json_field(jh_json_field(facts, "hal"), "targetSelector")),
      variant,
      &item->required,
      &empty};
  if (status == HAL_OK && h->memory.status == HAL_OK)
    status = jh_project_build_evaluate(&options, &item->build, failure);
  if (status != HAL_OK && item->build.rejected && skip_rejected) {
    evaluated_destroy(item);
    item = NULL;
    jh_failure_destroy(failure);
    jh_failure_init(failure);
    status = HAL_OK;
  } else if (status == HAL_OK) {
    status = jb_vector_append(builds, &item, 1);
    if (status == HAL_OK)
      item = NULL;
  }
  evaluated_destroy(item);
  jh_list_destroy(&empty);
  return status;
}

static hal_status_t assembly_declarations(jh_hw_t *h, const char *project,
                                          jh_failure_t *failure) {
  jb_vector_t directives = {0};
  (void)jh_config_directives_init(&directives);
  const char *header = jh_hw_path(h, project, "hal_project_config.h");
  hal_status_t status =
      jh_project_config_directives(header, &directives, failure);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&directives); ++i) {
    const jh_config_directive_t *item = jb_vector_const_at(&directives, i);
    if (!item->undef &&
        strcmp(jb_string_data(&item->name), "JH_PROJECT_TARGETS") == 0)
      status = jh_check_fail(
          failure,
          JH_PARTS(jh_span(header), jh_span(":"),
                   jh_span(jh_node_index(&h->memory, item->line)),
                   jh_span(": [JH-HW-TARGETS] a device tree takes its targets "
                           "from the assemblies; remove JH_PROJECT_TARGETS")));
  }
  jh_config_directives_destroy(&directives);
  return status;
}

static hal_status_t assembly_builds(batch_t *b, const char *project,
                                    const char *path,
                                    const jb_vector_t *variants,
                                    jb_vector_t *builds,
                                    jh_failure_t *failure) {
  jh_hw_t *h = b->h;
  jh_hardware_t listed = {0};
  hal_status_t status = jh_hardware_load(&listed, h->registry, path, failure);
  jb_vector_t names = {0};
  (void)jh_list_init(&names);
  jb_json_tree_view_t assemblies = {0};
  if (status == HAL_OK) {
    const jh_hw_t *loaded = listed.state;
    assemblies = jh_json_field(loaded->data, "assemblies");
    jh_hw_keys(h, assemblies, &names);
  }
  for (size_t i = 0; status == HAL_OK && h->memory.status == HAL_OK &&
                     i < jb_vector_size(&names);
       ++i) {
    const char *name =
        jh_hw_join(h, JH_PARTS(jh_string_span(jh_list_at(&names, i))));
    const char *selected_target =
        jh_hw_text(h, jh_json_field(jh_json_field(assemblies, name), "target"));
    if (jh_board_registry_find(&h->registry->boards.targets,
                               jh_span(selected_target)) == NULL) {
      status = jh_check_fail(
          failure,
          JH_PARTS(jh_span("[JH-CFG-TARGET] assembly "), jh_span(name),
                   jh_span(" names unknown target "),
                   jh_span(jh_hw_repr(
                       h, jh_hw_string(h, jh_span(selected_target))))));
      break;
    }
    jh_hardware_t hardware = {0};
    status = jh_hardware_load(&hardware, h->registry, path, failure);
    if (status == HAL_OK)
      status = jh_hardware_resolve(&hardware, NULL, name, failure);
    jb_string_t early = {0};
    (void)jb_string_init(&early, jb_heap_allocator());
    if (status == HAL_OK)
      status = jh_hardware_header(&hardware, &early, failure);
    if (status == HAL_OK && b->scratch == NULL)
      status = jh_process_scratch(&b->scratch, failure);
    const char *preinclude =
        b->scratch != NULL
            ? jh_hw_path(h, jb_temp_dir_path(b->scratch), "jh_hardware.h")
            : "";
    bool changed = false;
    if (status == HAL_OK)
      status = jh_project_build_write(preinclude, jh_string_span(&early),
                                      &changed, failure);
    const jb_json_tree_view_t selection =
        jh_json_field(jh_hardware_model(&hardware), "selection");
    const char *target = jh_hw_text(h, jh_json_field(selection, "target"));
    const char *board = jh_hw_text(h, jh_json_field(selection, "board"));
    for (size_t j = 0; status == HAL_OK && j < jb_vector_size(variants); ++j)
      status = evaluate(b, project, target, board, name,
                        jb_string_data(jh_list_at(variants, j)), preinclude,
                        true, builds, failure);
    jb_string_destroy(&early);
    jh_hardware_destroy(&hardware);
  }
  jh_list_destroy(&names);
  jh_hardware_destroy(&listed);
  return status;
}

static hal_status_t read_settings(jh_hw_t *h, const char *path,
                                  jb_json_tree_t **tree, bool *exists,
                                  jh_failure_t *failure) {
  const hal_status_t status = jh_json_read_settings(
      &h->where, path, h->registry->max_bytes, tree, exists, failure);
  if (status != HAL_OK && h->where.located) {
    jb_string_t location = {0};
    (void)jb_string_init(&location, jb_heap_allocator());
    jh_put_span(&location, &h->memory.status, jh_string_span(&h->where.source));
    jh_put(&location, &h->memory.status, ":");
    jh_put_int64(&location, &h->memory.status, (int64_t)h->where.line);
    jh_put(&location, &h->memory.status, ":");
    jh_put_int64(&location, &h->memory.status, (int64_t)h->where.column);
    if (h->memory.status == HAL_OK)
      (void)jh_fail(failure, status, failure->exit_code,
                    JH_PARTS(jh_span("invalid JSON in "),
                             jh_string_span(&location), jh_span(": "),
                             jh_string_span(&failure->message)));
    jb_string_destroy(&location);
  }
  return status;
}

static void project(batch_t *b, const char *directory, bool manifest_project) {
  jh_hw_t *h = b->h;
  jh_failure_t failure;
  jh_failure_init(&failure);
  jb_json_tree_t *manifest_tree = NULL;
  jb_json_tree_view_t manifest = {0};
  bool manifest_exists = false;
  const char *tree_path = jh_hw_path(h, directory, "device_tree.json");
  jb_fs_info_t info = {0};
  hal_status_t status = jb_fs_status(tree_path, true, &info);
  const bool assembly = status == HAL_OK && info.kind == JB_FS_REGULAR;
  if (status == HAL_ENOMEM || status == HAL_EOVERFLOW)
    (void)jh_fail_storage(&failure, status);
  else
    status = HAL_OK;
  if (status == HAL_OK && manifest_project)
    status = read_settings(
        h, jh_hw_path(h, directory, ".vscode/jaszczurhal.project.json"),
        &manifest_tree, &manifest_exists, &failure);
  if (manifest_tree != NULL)
    (void)jb_json_tree_root(manifest_tree, &manifest);
  jh_project_config_t declarations;
  jh_project_config_init(&declarations);
  if (status == HAL_OK && assembly)
    status = assembly_declarations(h, directory, &failure);
  if (status == HAL_OK)
    status =
        jh_project_config_read(jh_hw_path(h, directory, "hal_project_config.h"),
                               NULL, "", NULL, &declarations, &failure);
  jb_vector_t variants = {0}, targets = {0}, builds = {0};
  jh_node_storage(&h->memory, jh_list_init(&variants));
  jh_node_storage(&h->memory, jh_list_init(&targets));
  jh_node_storage(&h->memory, jb_vector_init(&builds, sizeof(evaluated_t *),
                                             jb_heap_allocator()));
  jh_node_storage(&h->memory, jh_list_push(&variants, jh_span("")));
  for (size_t i = 0; i < jb_vector_size(&declarations.variants); ++i) {
    const jh_config_variant_t *variant =
        jb_vector_const_at(&declarations.variants, i);
    jh_node_storage(&h->memory,
                    jh_list_push(&variants, jh_string_span(&variant->id)));
  }
  if (status == HAL_OK && h->memory.status == HAL_OK && assembly)
    status =
        assembly_builds(b, directory, tree_path, &variants, &builds, &failure);
  if (!assembly) {
    for (size_t i = 0; i < jb_vector_size(&declarations.targets); ++i) {
      const jb_json_span_t selector =
          jh_string_span(jh_list_at(&declarations.targets, i));
      for (size_t j = jb_vector_size(&h->registry->boards.targets); j != 0;
           --j) {
        const jh_board_descriptor_t *target =
            jb_vector_const_at(&h->registry->boards.targets, j - 1);
        if (jh_span_equal(
                selector,
                jh_span(jh_hw_text(
                    h, jh_json_field(jh_json_field(target->root, "hal"),
                                     "targetSelector"))))) {
          jh_node_storage(&h->memory,
                          jh_list_push(&targets, jh_string_span(&target->id)));
          break;
        }
      }
    }
    if (jb_vector_size(&targets) == 0) {
      const char *base = target_text(h, jh_json_field(manifest, "target"));
      if (*base)
        jh_node_storage(&h->memory, jh_list_push(&targets, jh_span(base)));
      const jb_json_tree_view_t profiles =
          jh_json_field(manifest, "targetProfiles");
      for (size_t i = 0; i < jh_json_count(profiles); ++i) {
        const char *name = jh_hw_key(h, jh_json_item(profiles, i));
        if (*name && !jh_list_contains(&targets, jh_span(name)))
          jh_node_storage(&h->memory, jh_list_push(&targets, jh_span(name)));
      }
      jh_list_sort(&targets);
      if (jb_vector_size(&targets) == 0)
        jh_node_storage(&h->memory, jh_list_push(&targets, jh_span("")));
    }
    for (size_t i = 0; status == HAL_OK && h->memory.status == HAL_OK &&
                       i < jb_vector_size(&targets);
         ++i)
      for (size_t j = 0; status == HAL_OK && j < jb_vector_size(&variants); ++j)
        status = evaluate(b, directory, jb_string_data(jh_list_at(&targets, i)),
                          NULL, NULL, jb_string_data(jh_list_at(&variants, j)),
                          "", true, &builds, &failure);
  }
  if (status != HAL_OK) {
    if (failure.status == HAL_OK)
      (void)jh_fail_storage(&failure, status);
    finding(b, directory, NULL, &failure);
  } else {
    for (size_t i = 0;
         h->memory.status == HAL_OK && i < jb_vector_size(&builds); ++i) {
      evaluated_t *item = *(evaluated_t **)jb_vector_at(&builds, i);
      const char *board = item->board;
      const char *available_label =
          label(h, item->target, item->assembly, item->variant);
      if (!manifest_project && jb_vector_size(&item->build.requested) == 0)
        continue;
      if (manifest_project) {
        jb_json_tree_t *settings_tree = NULL;
        jb_json_tree_view_t settings = {0};
        bool exists = false;
        (void)read_settings(h,
                            jh_hw_path(h, directory, ".vscode/settings.json"),
                            &settings_tree, &exists, &failure);
        if (settings_tree != NULL)
          (void)jb_json_tree_root(settings_tree, &settings);
        if (failure.status == HAL_OK)
          manifest_check(h, manifest, assembly, tree_path, &failure);
        const char *target =
            jh_effective_target(h, manifest, settings, item->target);
        if (failure.status == HAL_OK)
          jh_effective_sections(h, manifest, settings, target, assembly, false,
                                &failure);
        if (failure.status == HAL_OK)
          board = manifest_board(h, manifest, target, item->board, &failure);
        if (failure.status == HAL_OK)
          jh_effective_checkout(h, directory, manifest, settings, target,
                                item->assembly, &failure);
        if (failure.status == HAL_OK)
          jh_effective_sections(h, manifest, settings, target, false,
                                item->variant != NULL && *item->variant,
                                &failure);
        if (failure.status == HAL_OK && strcmp(target, item->target) != 0) {
          jb_vector_t selected = {0};
          (void)jb_vector_init(&selected, sizeof(evaluated_t *),
                               jb_heap_allocator());
          status = evaluate(b, directory, target, board, NULL, item->variant,
                            "", false, &selected, &failure);
          if (status == HAL_OK && jb_vector_size(&selected) == 1) {
            evaluated_destroy(item);
            item = *(evaluated_t **)jb_vector_at(&selected, 0);
            *(evaluated_t **)jb_vector_at(&builds, i) = item;
          } else if (failure.status == HAL_OK) {
            (void)jh_fail_storage(&failure,
                                  status == HAL_OK ? HAL_EINTERNAL : status);
          }
          jb_vector_destroy(&selected);
        }
        jb_json_tree_destroy(settings_tree);
      }
      if (failure.status == HAL_OK)
        record(b, directory, item->target, board, item->assembly, item->variant,
               &item->build, &item->required);
      else {
        finding(b, directory, available_label, &failure);
        jh_failure_destroy(&failure);
        jh_failure_init(&failure);
      }
    }
  }
  for (size_t i = 0; i < jb_vector_size(&builds); ++i)
    evaluated_destroy(*(evaluated_t **)jb_vector_at(&builds, i));
  jb_vector_destroy(&builds);
  jh_list_destroy(&targets);
  jh_list_destroy(&variants);
  jh_project_config_destroy(&declarations);
  jb_json_tree_destroy(manifest_tree);
  jh_failure_destroy(&failure);
}

hal_status_t jh_effective_features(const jh_hardware_registry_t *hardware,
                                   const jh_feature_registry_t *features,
                                   const jb_vector_t *roots,
                                   const jb_vector_t *inputs, jb_string_t *out,
                                   size_t *count, jb_vector_t *findings,
                                   jh_failure_t *failure) {
  if (hardware == NULL || features == NULL || roots == NULL || inputs == NULL ||
      out == NULL || count == NULL || findings == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = calloc(1, sizeof *h);
  if (h == NULL)
    return jh_fail_storage(failure, HAL_ENOMEM);
  jh_hw_init(h, hardware, failure);
  batch_t b = {h, features, roots, findings, {0}, NULL};
  jh_node_storage(&h->memory,
                  jb_vector_init(&b.records, sizeof(jb_json_tree_view_t),
                                 jb_heap_allocator()));
  jb_vector_t projects = {0}, standalone = {0};
  jh_node_storage(&h->memory, jh_list_init(&projects));
  jh_node_storage(&h->memory, jh_list_init(&standalone));
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(inputs);
       ++i) {
    const char *path = jb_string_data(jh_list_at(inputs, i));
    const char *directory = parent(h, path);
    if (strcmp(basename_of(path), "jaszczurhal.project.json") == 0 &&
        strcmp(basename_of(directory), ".vscode") == 0)
      jh_node_storage(&h->memory,
                      jh_list_push(&projects, jh_span(parent(h, directory))));
    else if (strcmp(basename_of(path), "hal_project_config.h") == 0)
      jh_node_storage(&h->memory,
                      jh_list_push(&standalone, jh_span(directory)));
  }
  jh_list_sort(&projects);
  jh_list_sort(&standalone);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&projects); ++i)
    project(&b, jb_string_data(jh_list_at(&projects, i)), true);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&standalone); ++i)
    if (!jh_list_contains(&projects,
                          jh_string_span(jh_list_at(&standalone, i))))
      project(&b, jb_string_data(jh_list_at(&standalone, i)), false);
  jh_node_storage(&h->memory, jb_temp_dir_remove(&b.scratch));
  jb_string_t serialized = {0};
  (void)jb_string_init(&serialized, jb_heap_allocator());
  if (h->memory.status == HAL_OK)
    report(&b, &serialized);
  if (h->memory.status == HAL_OK) {
    jh_node_storage(&h->memory,
                    jb_string_assign(out, jb_string_data(&serialized),
                                     jb_string_size(&serialized)));
    *count = jb_vector_size(&b.records);
  }
  jb_string_destroy(&serialized);
  jh_list_destroy(&standalone);
  jh_list_destroy(&projects);
  jb_vector_destroy(&b.records);
  const hal_status_t status = jh_hw_phase(h);
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  return status;
}

#include "jh_build_support.h"
#include "jh_cli_internal.h"
#include "jh_json_digest.h"
#include "jh_library_build.h"
#include <jb/filesystem.h>
#include <jb/sha256.h>
#include <stdlib.h>
#include <string.h>

/* Targets with a linkable-library provider, as the library runners select
 * them; the host provider has none. */
static const char *const library_providers[] = {
    "pico-sdk", "jh-stm32-baremetal", "esp-idf"};

static int compare_descriptors(const void *left, const void *right) {
  const jh_board_descriptor_t *a = *(const jh_board_descriptor_t *const *)left;
  const jh_board_descriptor_t *b = *(const jh_board_descriptor_t *const *)right;
  return strcmp(jb_string_data(&a->id), jb_string_data(&b->id));
}

/* The registry targets sorted by ID, as Python's sorted() orders them. */
static bool sorted_targets(jh_cli_t *ctx, const jh_board_registry_t *registry,
                           jb_vector_t *out) {
  if (!jh_cli_ok(ctx, jb_vector_init(out, sizeof(const jh_board_descriptor_t *),
                                     jb_heap_allocator())))
    return false;
  for (size_t i = 0; i < jb_vector_size(&registry->targets); ++i) {
    const jh_board_descriptor_t *item =
        jb_vector_const_at(&registry->targets, i);
    if (!jh_cli_ok(ctx, jb_vector_append(out, &item, 1)))
      return false;
  }
  if (jb_vector_size(out) > 1)
    qsort(jb_vector_data(out), jb_vector_size(out),
          sizeof(const jh_board_descriptor_t *), compare_descriptors);
  return true;
}

static const jh_board_descriptor_t *sorted_at(const jb_vector_t *list,
                                              size_t index) {
  return *(const jh_board_descriptor_t *const *)jb_vector_const_at(list, index);
}

int jh_cli_target_list(jh_cli_t *ctx) {
  jh_board_registry_t registry = {0};
  jh_board_registry_init(&registry);
  jb_vector_t sorted = {0};
  jb_string_t text = {0};
  (void)jh_cli_load_boards(ctx, "target list", &registry);
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  if (ctx->failure.status != HAL_OK || !sorted_targets(ctx, &registry, &sorted))
    goto done;
  if (ctx->json) {
    jh_result_begin(ctx, 4096 + 256 * jb_vector_size(&sorted),
                    32 + 4 * jb_vector_size(&sorted));
    jh_result_text(ctx, "command", "target.list");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_array(ctx, "targets");
  }
  hal_status_t status = HAL_OK;
  for (size_t i = 0; i < jb_vector_size(&sorted) && status == HAL_OK; ++i) {
    const jh_board_descriptor_t *item = sorted_at(&sorted, i);
    const jb_json_span_t provider =
        jh_build_descriptor_text(item->root, "build", "provider");
    bool library = false;
    for (size_t p = 0; p < COUNTOF(library_providers); ++p)
      library = library || (provider.length == strlen(library_providers[p]) &&
                            memcmp(provider.data, library_providers[p],
                                   provider.length) == 0);
    if (!library)
      continue;
    const jb_json_span_t state =
        jh_build_descriptor_text(item->root, "status", NULL);
    if (ctx->json) {
      jh_result_object(ctx, NULL);
      jh_result_string(ctx, "id", jh_string_span(&item->id));
      jh_result_string(ctx, "provider", provider);
      jh_result_string(ctx, "status", state);
      jh_result_object_end(ctx);
      continue;
    }
    jh_put_span(&text, &status, jh_string_span(&item->id));
    jh_put(&text, &status, " ");
    jh_put_span(&text, &status, provider);
    jh_put(&text, &status, " ");
    jh_put_span(&text, &status, state);
    jh_put(&text, &status, "\n");
  }
  if (ctx->json) {
    jh_result_array_end(ctx);
    (void)jh_result_finish(ctx);
  } else if (jh_cli_ok(ctx, status))
    (void)jh_cli_stdout(ctx, jh_string_span(&text));
done:
  jb_string_destroy(&text);
  jb_vector_destroy(&sorted);
  jh_board_registry_destroy(&registry);
  return 0;
}

/* The facts of one target as the library runners read them: KEY=VALUE lines
 * of the provider, default board, ISA, status and supported features (each
 * without a trailing =1, separated by spaces). */
int jh_cli_target_facts(jh_cli_t *ctx) {
  jh_board_registry_t registry = {0};
  jh_board_registry_init(&registry);
  jb_vector_t sorted = {0};
  jb_string_t text = {0}, known = {0};
  (void)jh_cli_load_boards(ctx, "target facts", &registry);
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jb_string_init(&known, jb_heap_allocator()));
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_require(ctx, ctx->args.present[opt_target] ? HAL_OK : HAL_EINVAL,
                      2, "target facts requires --target") ||
      !sorted_targets(ctx, &registry, &sorted))
    goto done;
  const char *wanted = ctx->args.values[opt_target];
  const jh_board_descriptor_t *selected = NULL;
  hal_status_t status = HAL_OK;
  for (size_t i = 0; i < jb_vector_size(&sorted); ++i) {
    const jh_board_descriptor_t *item = sorted_at(&sorted, i);
    if (strcmp(jb_string_data(&item->id), wanted) == 0)
      selected = item;
    if (i != 0)
      jh_put(&known, &status, ", ");
    jh_put_span(&known, &status, jh_string_span(&item->id));
  }
  if (!jh_cli_ok(ctx, status))
    goto done;
  if (selected == NULL) {
    (void)jh_cli_error(
        ctx, HAL_ENOENT, 2,
        JH_PARTS(jh_span("unknown target "), jh_cli_repr(ctx, jh_span(wanted)),
                 jh_span("; known targets: "), jh_string_span(&known)));
    goto done;
  }
  const jb_json_span_t provider = jh_build_descriptor_text(selected->root,
                                                           "build", "provider"),
                       board = jh_build_descriptor_text(selected->root,
                                                        "defaultBoard", NULL),
                       isa = jh_build_descriptor_text(selected->root,
                                                      "architecture", "isa"),
                       state = jh_build_descriptor_text(selected->root,
                                                        "status", NULL);
  const jb_json_tree_view_t features =
      jh_json_field(selected->root, "supportedFeatures");
  size_t feature_count = 0;
  if (features.tree != NULL &&
      jb_json_tree_count(features, &feature_count) != HAL_OK)
    feature_count = 0;
  if (ctx->json) {
    jh_result_begin(ctx, 8192 + 64 * feature_count, 64 + feature_count);
    jh_result_text(ctx, "command", "target.facts");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "target", wanted);
    jh_result_string(ctx, "provider", provider);
    jh_result_string(ctx, "defaultBoard", board);
    jh_result_string(ctx, "isa", isa);
    jh_result_string(ctx, "targetStatus", state);
    jh_result_array(ctx, "supportedFeatures");
  } else {
    jh_put(&text, &status, "provider=");
    jh_put_span(&text, &status, provider);
    jh_put(&text, &status, "\ndefaultBoard=");
    jh_put_span(&text, &status, board);
    jh_put(&text, &status, "\nisa=");
    jh_put_span(&text, &status, isa);
    jh_put(&text, &status, "\nstatus=");
    jh_put_span(&text, &status, state);
    jh_put(&text, &status, "\nsupportedFeatures=");
  }
  for (size_t i = 0; i < feature_count && status == HAL_OK; ++i) {
    jb_json_span_t item = {NULL, 0};
    jb_json_tree_view_t element = {0};
    if (jb_json_tree_at(features, i, &element) != HAL_OK ||
        jb_json_tree_text(element, &item) != HAL_OK)
      continue;
    if (item.length >= 2 && memcmp(item.data + item.length - 2, "=1", 2) == 0)
      item.length -= 2;
    if (ctx->json)
      jh_result_item(ctx, item);
    else {
      if (i != 0)
        jh_put(&text, &status, " ");
      jh_put_span(&text, &status, item);
    }
  }
  if (ctx->json) {
    jh_result_array_end(ctx);
    (void)jh_result_finish(ctx);
  } else {
    jh_put(&text, &status, "\n");
    if (jh_cli_ok(ctx, status))
      (void)jh_cli_stdout(ctx, jh_string_span(&text));
  }
done:
  jb_string_destroy(&known);
  jb_string_destroy(&text);
  jb_vector_destroy(&sorted);
  jh_board_registry_destroy(&registry);
  return 0;
}

/* The build directory key of a device tree library (jh_library_key). */
int jh_cli_library_key(jh_cli_t *ctx) {
  jb_string_t key = {0};
  (void)jh_cli_ok(ctx, jb_string_init(&key, jb_heap_allocator()));
  if (!jh_cli_require(
          ctx, ctx->args.present[opt_project_directory] ? HAL_OK : HAL_EINVAL,
          2, "library key requires --config-dir"))
    goto done;
  const char *config = ctx->args.values[opt_project_directory];
  if (!jh_cli_ok(ctx, jh_library_key(
                          config, ctx->base, ctx->args.values[opt_assembly],
                          &ctx->args.lists[opt_request], &key, &ctx->failure)))
    goto done;
  if (ctx->json) {
    jh_result_begin(ctx, strlen(config) + jb_string_size(&key) + 1024, 32);
    jh_result_text(ctx, "command", "library.key");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_string(ctx, "key", jh_string_span(&key));
    (void)jh_result_finish(ctx);
  } else
    (void)jh_cli_stdout(
        ctx, jh_cli_join(ctx, JH_PARTS(jh_string_span(&key), jh_span("\n"))));
done:
  jb_string_destroy(&key);
  return 0;
}

/* The HAL checkout as `cd .. && pwd` of the runners named it: absolute and
 * normalized lexically, without . or .. segments or a trailing separator. */
static const char *normalized_checkout(jh_cli_t *ctx) {
  const char *root = jh_cli_checkout(ctx);
  size_t required = 0;
  if (ctx->failure.status != HAL_OK)
    return "";
  hal_status_t status = jb_fs_path_normalize(root, NULL, 0, &required);
  if (status != HAL_OK && status != HAL_EOVERFLOW) {
    (void)jh_cli_ok(ctx, status);
    return "";
  }
  jb_string_t text = {0};
  status = jb_string_init(&text, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_string_resize(&text, required, '\0');
  if (status == HAL_OK)
    status = jb_fs_path_normalize(root, jb_string_mutable_data(&text), required,
                                  &required);
  if (status == HAL_OK)
    status = jb_string_resize(&text, required - 1, '\0');
  const char *result = jh_cli_ok(ctx, status)
                           ? jh_cli_text(ctx, JH_PARTS(jh_string_span(&text)))
                           : "";
  jb_string_destroy(&text);
  return result;
}

static const char *optional(const jh_cli_t *ctx, jh_option_id_t option) {
  return ctx->args.present[option] ? ctx->args.values[option] : NULL;
}

/* The linkable library of one target, built by the CMake project of its
 * provider (jh_library_build). */
int jh_cli_library_build(jh_cli_t *ctx) {
  jh_board_registry_t registry = {0};
  jh_board_registry_init(&registry);
  jh_library_build_result_t result;
  jh_library_build_result_init(&result);
  (void)jh_cli_load_boards(ctx, "library build", &registry);
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_require(ctx, ctx->args.present[opt_target] ? HAL_OK : HAL_EINVAL,
                      2, "library build requires --target") ||
      !jh_cli_install_cancellation(ctx))
    goto done;
  jh_library_build_options_t options = {0};
  options.root = normalized_checkout(ctx);
  options.base = ctx->base;
  options.target = ctx->args.values[opt_target];
  options.board = ctx->args.values[opt_board_option];
  options.assembly = ctx->args.values[opt_assembly];
  options.variant = ctx->args.values[opt_variant_option];
  options.project_config = optional(ctx, opt_project_config);
  options.defines = &ctx->args.lists[opt_define];
  options.all_features = ctx->args.present[opt_all_features];
  /* --all-features also enables FreeRTOS, as the runners did. */
  options.freertos = ctx->args.present[opt_freertos] || options.all_features;
  options.library_only = ctx->args.present[opt_library_only];
  options.clean = ctx->args.present[opt_clean];
  options.output = ctx->args.values[opt_output];
  options.jobs = optional(ctx, opt_jobs);
  options.sdk_dir = optional(ctx, opt_sdk_dir);
  options.toolchain = optional(ctx, opt_toolchain);
  options.picotool_dir = optional(ctx, opt_picotool_dir);
  options.picotool_build_dir = optional(ctx, opt_picotool_build_dir);
  options.example = optional(ctx, opt_example);
  options.freertos_kernel = optional(ctx, opt_freertos_kernel);
  options.idf_dir = optional(ctx, opt_idf_dir);
  options.log_to_stderr = ctx->json;
  options.canceled = jh_cli_canceled;
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx,
                 jh_library_build(&options, &registry, &result, &ctx->failure)))
    goto done;
  if (ctx->json) {
    jh_result_begin(ctx, jb_string_size(&result.output) * 2 + 2048, 32);
    jh_result_text(ctx, "command", "library.build");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "target", options.target);
    jh_result_string(ctx, "selection", jh_string_span(&result.selection));
    jh_result_boolean(ctx, "deviceTree", result.device_tree);
    jh_result_string(ctx, "output", jh_string_span(&result.output));
    jh_result_string(ctx, "library", jh_string_span(&result.library));
    (void)jh_result_finish(ctx);
  }
done:
  jh_library_build_result_destroy(&result);
  jh_board_registry_destroy(&registry);
  return 0;
}

int jh_cli_library_install(jh_cli_t *ctx) {
  if (!jh_cli_require(ctx,
                      ctx->args.present[opt_build_dir] ? HAL_OK : HAL_EINVAL, 2,
                      "library install requires --build-dir") ||
      !jh_cli_require(ctx, ctx->args.present[opt_prefix] ? HAL_OK : HAL_EINVAL,
                      2, "library install requires --prefix") ||
      !jh_cli_install_cancellation(ctx))
    return 0;
  const char *build =
      jh_cli_path(ctx, ctx->args.values[opt_build_dir], ctx->base, false);
  const char *prefix =
      jh_cli_path(ctx, ctx->args.values[opt_prefix], ctx->base, false);
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_library_install(build, prefix, ctx->base, ctx->json,
                                         jh_cli_canceled, NULL, &ctx->failure)))
    return 0;
  if (ctx->json) {
    jh_result_begin(ctx, strlen(build) + strlen(prefix) * 2 + 1024, 32);
    jh_result_text(ctx, "command", "library.install");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "buildDir", build);
    jh_result_text(ctx, "prefix", prefix);
    (void)jh_result_finish(ctx);
  } else
    (void)jh_cli_stdout(
        ctx,
        jh_cli_join(ctx, JH_PARTS(jh_span("Installed JaszczurHAL library to "),
                                  jh_span(prefix), jh_span("\n"))));
  return 0;
}

typedef struct {
  jh_cli_t *ctx;
  jb_vector_t *files; /* Relative paths, jb_string_t. */
  jb_string_t prefix, relative;
} package_walk_t;

static hal_status_t walk_package(package_walk_t *walk);

static hal_status_t package_entry(void *context, const char *name,
                                  const jb_fs_info_t *info) {
  package_walk_t *walk = context;
  const size_t saved = jb_string_size(&walk->relative);
  hal_status_t status = HAL_OK;
  if (saved != 0)
    jh_put(&walk->relative, &status, "/");
  jh_put(&walk->relative, &status, name);
  if (status == HAL_OK && info->kind == JB_FS_DIRECTORY)
    status = walk_package(walk);
  else if (status == HAL_OK && info->kind == JB_FS_REGULAR)
    status = jh_list_push(walk->files, jh_string_span(&walk->relative));
  if (status == HAL_OK)
    status = jb_string_resize(&walk->relative, saved, '\0');
  return status;
}

static hal_status_t walk_package(package_walk_t *walk) {
  jb_string_t path = {0};
  hal_status_t status = jb_string_init(&path, jb_heap_allocator());
  jh_put_span(&path, &status, jh_string_span(&walk->prefix));
  if (jb_string_size(&walk->relative) != 0) {
    jh_put(&path, &status, "/");
    jh_put_span(&path, &status, jh_string_span(&walk->relative));
  }
  if (status == HAL_OK)
    status = jb_fs_list(jb_string_data(&path), package_entry, walk);
  jb_string_destroy(&path);
  return status;
}

static int compare_strings(const void *left, const void *right) {
  return strcmp(jb_string_data(left), jb_string_data(right));
}

static const char *file_sha256(jh_cli_t *ctx, const char *path) {
  jb_file_data_t data = {0};
  if (!jh_cli_ok(ctx, jb_file_read(path, 512u * 1024u * 1024u, &data)))
    return "";
  jb_sha256_context_t sha = {0};
  jb_sha256_init(&sha);
  if (data.length != 0)
    jb_sha256_update(&sha, data.data, data.length);
  jb_file_release(&data);
  uint8_t digest[JB_SHA256_DIGEST_BYTES];
  jb_sha256_final(&sha, digest);
  char hex[65];
  jh_sha256_hex(digest, hex);
  return jh_cli_text(ctx, JH_PARTS(jh_span(hex)));
}

/* set(NAME "value") of the device tree package record. */
static jb_json_span_t record_value(const jb_file_data_t *record,
                                   const char *name) {
  const char *text = (const char *)record->data;
  const size_t length = record->length, wanted = strlen(name);
  for (size_t i = 0; text != NULL && i + wanted + 6 < length; ++i) {
    if (memcmp(text + i, "set(", 4) != 0 ||
        memcmp(text + i + 4, name, wanted) != 0 ||
        text[i + 4 + wanted] != ' ' || text[i + 5 + wanted] != '"')
      continue;
    const size_t start = i + 6 + wanted;
    size_t end = start;
    while (end < length && text[end] != '"' && text[end] != '\n')
      ++end;
    return (jb_json_span_t){text + start, end - start};
  }
  return jh_span("");
}

/* The files of an installed library package with their SHA-256, its kind
 * (a board profile or a device tree) and the identity the device tree
 * record states. */
int jh_cli_library_inspect(jh_cli_t *ctx) {
  jb_vector_t files = {0};
  package_walk_t walk = {ctx, &files, {0}, {0}};
  jb_file_data_t record = {0};
  (void)jh_cli_ok(ctx, jh_list_init(&files));
  (void)jh_cli_ok(ctx, jb_string_init(&walk.prefix, jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jb_string_init(&walk.relative, jb_heap_allocator()));
  if (!jh_cli_require(ctx, ctx->args.present[opt_prefix] ? HAL_OK : HAL_EINVAL,
                      2, "library inspect requires --prefix"))
    goto done;
  const char *prefix =
      jh_cli_path(ctx, ctx->args.values[opt_prefix], ctx->base, false);
  const char *archive = jh_cli_text(
      ctx, JH_PARTS(jh_span(prefix), jh_span("/lib/libJaszczurHAL.a")));
  jb_fs_info_t entry = {0};
  if (ctx->failure.status != HAL_OK)
    goto done;
  if (jb_fs_status(archive, true, &entry) != HAL_OK ||
      entry.kind != JB_FS_REGULAR) {
    (void)jh_cli_error(
        ctx, HAL_ENOENT, 2,
        JH_PARTS(jh_span(prefix),
                 jh_span(" is not a library package: lib/libJaszczurHAL.a "
                         "is missing")));
    goto done;
  }
  const char *share = jh_cli_text(
      ctx, JH_PARTS(jh_span(prefix), jh_span("/share/JaszczurHAL/generated/")));
  const char *board_record = jh_cli_text(
      ctx, JH_PARTS(jh_span(share), jh_span("jh_board_resolved.json")));
  const char *tree_record = jh_cli_text(
      ctx, JH_PARTS(jh_span(share), jh_span("jh_hardware_config.cmake")));
  const char *kind = "unknown";
  if (jb_fs_status(tree_record, true, &entry) == HAL_OK &&
      entry.kind == JB_FS_REGULAR) {
    kind = "device-tree";
    if (!jh_cli_ok(ctx,
                   jb_file_read(tree_record, 16u * 1024u * 1024u, &record)))
      goto done;
  } else if (jb_fs_status(board_record, true, &entry) == HAL_OK &&
             entry.kind == JB_FS_REGULAR)
    kind = "board";
  if (!jh_cli_ok(ctx, jb_string_assign(&walk.prefix, prefix, strlen(prefix))) ||
      !jh_cli_ok(ctx, walk_package(&walk)))
    goto done;
  if (jb_vector_size(&files) > 1)
    qsort(jb_vector_data(&files), jb_vector_size(&files), sizeof(jb_string_t),
          compare_strings);
  static const char *const identity[] = {
      "JH_CONFIG_SHA256", "JH_HARDWARE_SHA256", "JH_HARDWARE_ABI"};
  if (ctx->json) {
    jh_result_begin(ctx, strlen(prefix) + 512 * jb_vector_size(&files) + 4096,
                    64 + 4 * jb_vector_size(&files));
    jh_result_text(ctx, "command", "library.inspect");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "prefix", prefix);
    jh_result_text(ctx, "kind", kind);
    if (record.data != NULL) {
      jh_result_object(ctx, "record");
      for (size_t i = 0; i < COUNTOF(identity); ++i)
        jh_result_string(ctx, identity[i], record_value(&record, identity[i]));
      jh_result_object_end(ctx);
    }
    jh_result_array(ctx, "files");
  }
  jb_string_t text = {0};
  hal_status_t status = jb_string_init(&text, jb_heap_allocator());
  if (!ctx->json) {
    jh_put(&text, &status, "kind=");
    jh_put(&text, &status, kind);
    jh_put(&text, &status, "\n");
    for (size_t i = 0; record.data != NULL && i < COUNTOF(identity); ++i) {
      jh_put(&text, &status, identity[i]);
      jh_put(&text, &status, "=");
      jh_put_span(&text, &status, record_value(&record, identity[i]));
      jh_put(&text, &status, "\n");
    }
  }
  for (size_t i = 0; i < jb_vector_size(&files) && status == HAL_OK &&
                     ctx->failure.status == HAL_OK;
       ++i) {
    const jb_string_t *name = jh_list_at(&files, i);
    const char *digest = file_sha256(
        ctx, jh_cli_text(ctx, JH_PARTS(jh_span(prefix), jh_span("/"),
                                       jh_string_span(name))));
    if (ctx->json) {
      jh_result_object(ctx, NULL);
      jh_result_string(ctx, "path", jh_string_span(name));
      jh_result_text(ctx, "sha256", digest);
      jh_result_object_end(ctx);
    } else {
      jh_put(&text, &status, digest);
      jh_put(&text, &status, "  ");
      jh_put_span(&text, &status, jh_string_span(name));
      jh_put(&text, &status, "\n");
    }
  }
  if (ctx->json) {
    jh_result_array_end(ctx);
    (void)jh_result_finish(ctx);
  } else if (jh_cli_ok(ctx, status))
    (void)jh_cli_stdout(ctx, jh_string_span(&text));
  jb_string_destroy(&text);
done:
  jb_file_release(&record);
  jb_string_destroy(&walk.relative);
  jb_string_destroy(&walk.prefix);
  jh_list_destroy(&files);
  return 0;
}

#include "jh_hardware_internal.h"
#include "jh_project_config.h"
#include <string.h>

static jb_json_tree_view_t list_value(jh_hw_t *h, const jb_vector_t *list) {
  jb_json_tree_t *result = jh_hw_array(h);
  for (size_t i = 0; i < jb_vector_size(list); ++i)
    jh_hw_append(h, result,
                 jh_hw_string(h, jh_string_span(jh_list_at(list, i))));
  return jh_hw_root(result);
}

static void program_error(jh_hw_t *h, const char *code, const char *source,
                          const char *line, const char *argument,
                          const char *message) {
  if (h->memory.status != HAL_OK)
    return;
  h->memory.status = jh_fail(
      h->failure, HAL_ECONFIG, 2,
      JH_PARTS(jh_span("["), jh_span(code), jh_span("] "), jh_span(source),
               jh_span(":"), jh_span(line != NULL ? line : "/"), jh_span(": "),
               jh_span(message), jh_span(argument != NULL ? " (argument " : ""),
               jh_span(argument != NULL ? argument : ""),
               jh_span(argument != NULL ? ")" : "")));
  if (h->memory.status == HAL_ECONFIG) {
    hal_status_t stored = HAL_OK;
    jh_put(&h->failure->code, &stored, code);
    jh_put(&h->failure->source, &stored, source);
    jh_put(&h->failure->detail, &stored, message);
    if (argument != NULL)
      jh_put(&h->failure->argument, &stored, argument);
    if (line != NULL)
      for (const char *p = line; jh_ascii_is_digit(*p); ++p)
        h->failure->line = h->failure->line * 10 + (size_t)(*p - '0');
    if (stored != HAL_OK)
      h->memory.status = jh_fail_storage(h->failure, stored);
  }
}

static jb_json_tree_view_t macro(const jh_project_build_t *program,
                                 const char *name) {
  const jb_json_tree_view_t macros =
      jh_json_field(program->config_root, "macros");
  for (size_t i = 0; i < jh_json_count(macros); ++i) {
    const jb_json_tree_view_t item = jh_json_item(macros, i);
    if (jh_hw_is(jh_json_field(item, "name"), name))
      return item;
  }
  return (jb_json_tree_view_t){0};
}

static void origin_error(jh_hw_t *h, const jh_hardware_build_options_t *options,
                         const jh_project_build_t *program, const char *name,
                         const char *default_source, const char *message) {
  bool argument = false;
  for (size_t i = 0;
       options->definitions != NULL && i < jb_vector_size(options->definitions);
       ++i)
    if (strcmp(jh_hw_definition_name(
                   h, jh_string_span(jh_list_at(options->definitions, i))),
               name) == 0)
      argument = true;
  const jb_json_tree_view_t found = macro(program, name);
  const char *origin = jh_hw_text(h, jh_json_field(found, "source"));
  if (argument || found.tree == NULL || strcmp(origin, "predefined") == 0) {
    program_error(h, "JH-HW-FEATURE", default_source, NULL,
                  argument ? "--define" : "--variant", message);
  } else {
    const char *colon = strrchr(origin, ':');
    const char *source =
        colon == NULL ? origin
                      : jh_hw_join(h, JH_PARTS(((jb_json_span_t){
                                          origin, (size_t)(colon - origin)})));
    program_error(h, "JH-HW-FEATURE", source, colon != NULL ? colon + 1 : NULL,
                  NULL, message);
  }
}

static void ownership(jh_hw_t *h, const jh_hardware_build_options_t *options,
                      const jh_project_build_t *program) {
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&program->definitions);
       ++i) {
    const char *name = jh_hw_definition_name(
        h, jh_string_span(jh_list_at(&program->definitions, i)));
    if (jh_hw_owned(name))
      origin_error(
          h, options, program, name, options->hardware_file,
          jh_hw_join(h, JH_PARTS(jh_span(name),
                                 jh_span(" belongs to a generated header"))));
  }
  const jb_json_tree_view_t files =
      jh_json_field(program->config_root, "files");
  const jb_json_tree_view_t directives =
      jh_json_field(program->config_root, "directives");
  jb_vector_t sorted = {0};
  jh_node_storage(&h->memory, jh_list_init(&sorted));
  for (size_t i = 0; i < jh_json_count(files); ++i)
    jh_node_storage(
        &h->memory,
        jh_list_push(&sorted, jh_span(jh_hw_text(h, jh_json_item(files, i)))));
  jh_list_sort(&sorted);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&sorted);
       ++i) {
    const char *file = jb_string_data(jh_list_at(&sorted, i));
    if (strcmp(file, options->header_file) == 0)
      continue;
    for (size_t j = 0;
         h->memory.status == HAL_OK && j < jh_json_count(directives); ++j) {
      const jb_json_tree_view_t item = jh_json_item(directives, j);
      const char *name = jh_hw_text(h, jh_json_field(item, "name"));
      if (jh_hw_is(jh_json_field(item, "file"), file) && jh_hw_owned(name))
        program_error(
            h, "JH-HW-FEATURE", file,
            jh_hw_repr(h, jh_json_field(item, "line")), NULL,
            jh_hw_join(
                h, JH_PARTS(
                       jh_span("#"),
                       jh_span(jh_hw_text(h, jh_json_field(item, "directive"))),
                       jh_span(" "), jh_span(name),
                       jh_span(": the macro belongs to a generated header"))));
    }
  }
  jh_list_destroy(&sorted);
}

static void hardware_requirements(jh_hw_t *h,
                                  const jh_hardware_build_options_t *options,
                                  jb_json_tree_view_t document,
                                  const jh_project_build_t *program,
                                  const jb_vector_t *features) {
  const jb_json_tree_view_t selection = jh_json_field(document, "selection");
  if (jh_hw_is(jh_json_field(selection, "target"), "mock"))
    return;
  const char *file =
      jh_hw_path(h, options->config_directory, "device_tree.json");
  jh_json_document_t input = {0};
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_json_read_document(
        &h->where, file, h->registry->max_bytes, true, &input, h->failure);
  jb_json_node_t root = {0};
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_root(&input.doc, &root));
  const jb_json_tree_view_t data = jh_hw_import(h, root);
  const char *assembly = jh_hw_text(h, jh_json_field(selection, "assembly"));
  const jb_json_tree_view_t providers = jh_json_field(
      jh_json_field(jh_json_field(data, "assemblies"), assembly), "providers");
  const jb_json_tree_view_t capabilities =
      jh_json_field(jh_json_field(document, "hardware"), "capabilities");
  static const struct {
    const char *name, *caps[2];
  } required[] = {
      {"HAL_ENABLE_BLE", {"bluetooth-le-controller", NULL}},
      {"HAL_ENABLE_BLUETOOTH_CLASSIC", {"bluetooth-classic-controller", NULL}},
      {"HAL_ENABLE_WIFI", {"cyw43", "native-wifi"}},
      {"HAL_NETWORK_BACKEND_CYW43", {"cyw43", NULL}},
      {"HAL_NETWORK_BACKEND_ESP_IDF", {"native-wifi", NULL}}};
  for (size_t i = 0; h->memory.status == HAL_OK && i < COUNTOF(required); ++i) {
    const char *name = required[i].name;
    if (!jh_list_contains(features, jh_span(name)) &&
        macro(program, name).tree == NULL)
      continue;
    bool present = false;
    const char *chosen = NULL;
    jb_json_tree_t *needed = jh_hw_array(h);
    for (size_t j = 0;
         j < COUNTOF(required[i].caps) && required[i].caps[j] != NULL; ++j) {
      const char *cap = required[i].caps[j];
      present = present || jh_hw_bool(jh_json_field(capabilities, cap));
      if (chosen == NULL && jh_json_field(providers, cap).tree != NULL)
        chosen = cap;
      jh_hw_append(h, needed, jh_hw_string(h, jh_span(cap)));
    }
    if (present)
      continue;
    if (chosen != NULL) {
      const char *at = jh_hw_pointer(
          h,
          jh_hw_pointer(h, jh_hw_pointer(h, "/assemblies", assembly),
                        "providers"),
          chosen);
      jh_hw_error(
          h, "JH-HW-FEATURE", (jh_hw_origin_t){file, at}, NULL,
          JH_PARTS(jh_span(name), jh_span(" needs "), jh_span(chosen),
                   jh_span(", and the selected provider "),
                   jh_span(jh_hw_text(h, jh_json_field(providers, chosen))),
                   jh_span(" is absent or not completely wired")));
      (void)jh_hw_phase(h);
    } else {
      origin_error(
          h, options, program, name, file,
          jh_hw_join(h, JH_PARTS(jh_span(name), jh_span(" needs one of "),
                                 jh_span(jh_hw_repr(h, jh_hw_root(needed))),
                                 jh_span(" in the assembly"))));
    }
  }
  jh_json_document_destroy(&input);
}

static jb_json_tree_view_t
target_abi(jh_hw_t *h, const jh_hardware_build_options_t *options) {
  const jb_json_tree_view_t target = h->target->root,
                            architecture =
                                jh_json_field(target, "architecture");
  jb_json_tree_t *abi = jh_hw_object(h);
  jh_hw_set(h, abi, "isa", jh_json_field(architecture, "isa"));
  jh_hw_set(h, abi, "fpu",
            jh_hw_integer(
                h, jh_hw_bool(jh_json_field(architecture, "hasFpu")) ? 1 : 0));
  const jb_json_tree_view_t build = jh_json_field(target, "build");
  jb_vector_t keys = {0};
  jh_node_storage(&h->memory, jh_list_init(&keys));
  jh_hw_keys(h, build, &keys);
  for (size_t i = 0; i < jb_vector_size(&keys); ++i) {
    const char *key = jb_string_data(jh_list_at(&keys, i));
    if (strcmp(key, "provider") != 0)
      jh_hw_set(h, abi, key, jh_json_field(build, key));
  }
  if (options->compiler != NULL) {
    jb_json_tree_view_t layout = {0};
    const jb_json_tree_view_t macros = jh_hw_compiler_abi(
        h, options->compiler, options->compiler_flags, &layout);
    jh_hw_set(h, abi, "compiler", macros);
    jh_hw_set(h, abi, "layout", layout);
  }
  jh_list_destroy(&keys);
  return jh_hw_root(abi);
}

jb_json_tree_view_t jh_hw_program(jh_hw_t *h,
                                  const jh_hardware_build_options_t *options,
                                  jb_json_tree_view_t document,
                                  jh_project_build_t *program) {
  const char *header =
      jh_hw_path(h, options->config_directory, "hal_project_config.h");
  jb_vector_t declarations = {0}, required = {0}, normalized = {0}, empty = {0};
  jh_node_storage(&h->memory, jh_config_directives_init(&declarations));
  jh_node_storage(&h->memory, jh_list_init(&required));
  jh_node_storage(&h->memory, jh_list_init(&normalized));
  jh_node_storage(&h->memory, jh_list_init(&empty));
  if (h->memory.status == HAL_OK)
    h->memory.status =
        jh_project_config_directives(header, &declarations, h->failure);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&declarations); ++i) {
    const jh_config_directive_t *directive =
        jb_vector_const_at(&declarations, i);
    if (!directive->undef &&
        strcmp(jb_string_data(&directive->name), "JH_PROJECT_TARGETS") == 0)
      program_error(h, "JH-HW-TARGETS", header,
                    jh_node_index(&h->memory, directive->line), NULL,
                    "a device tree takes its targets from the assemblies; "
                    "remove JH_PROJECT_TARGETS");
  }
  const jb_json_tree_view_t target = h->target->root,
                            required_features =
                                jh_json_field(target, "requiredFeatures");
  for (size_t i = 0; i < jh_json_count(required_features); ++i)
    jh_node_storage(
        &h->memory,
        jh_list_push(&required, jh_span(jh_hw_text(
                                    h, jh_json_item(required_features, i)))));
  const jh_project_build_options_t request = {
      header,
      options->header_file,
      jb_string_data(&h->target->id),
      jh_hw_text(h,
                 jh_json_field(jh_json_field(target, "hal"), "targetSelector")),
      options->variant != NULL ? options->variant : "",
      &required,
      options->definitions != NULL ? options->definitions : &empty};
  hal_status_t status = h->memory.status;
  if (status == HAL_OK)
    status = jh_project_build_evaluate(&request, program, h->failure);
  if (status == HAL_OK)
    status = jh_project_build_validate(&request, program, options->features,
                                       h->failure);
  if (status != HAL_OK && h->memory.status == HAL_OK) {
    const char *message =
        jh_hw_join(h, JH_PARTS(jh_string_span(&h->failure->message)));
    if (status == HAL_ENOMEM || status == HAL_EOVERFLOW)
      h->memory.status = status;
    else
      program_error(h, "JH-HW-FEATURE", header, NULL, NULL, message);
  }
  ownership(h, options, program);
  jh_feature_resolution_t resolution;
  jh_feature_resolution_init(&resolution);
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_board_normalize_features(&program->requested,
                                                   &normalized, h->failure);
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_board_resolve_features(
        options->features, target, &normalized, &resolution, h->failure);
  hardware_requirements(h, options, document, program, &resolution.resolved);
  jb_json_tree_t *software = jh_hw_object(h), *definitions = jh_hw_array(h),
                 *tunables = jh_hw_object(h);
  jh_hw_set(h, software, "variant", jh_json_field(program->variant, "id"));
  jb_vector_t sorted = {0};
  jh_node_storage(&h->memory, jh_list_init(&sorted));
  for (size_t i = 0; i < jb_vector_size(&program->requested); ++i)
    jh_node_storage(&h->memory,
                    jh_list_push(&sorted, jh_string_span(jh_list_at(
                                              &program->requested, i))));
  jh_list_sort(&sorted);
  jh_hw_set(h, software, "requestedFeatures", list_value(h, &sorted));
  jh_hw_set(h, software, "resolvedFeatures",
            list_value(h, &resolution.resolved));
  jh_list_destroy(&sorted);
  jh_node_storage(&h->memory, jh_list_init(&sorted));
  for (size_t i = 0; i < jb_vector_size(&program->definitions); ++i) {
    const jb_json_span_t item =
        jh_string_span(jh_list_at(&program->definitions, i));
    if (!jh_feature_symbol(jh_span(jh_hw_definition_name(h, item)), false))
      jh_node_storage(&h->memory, jh_list_push(&sorted, item));
  }
  jh_list_sort(&sorted);
  for (size_t i = 0; i < jb_vector_size(&sorted); ++i)
    jh_hw_append(h, definitions,
                 jh_hw_string(h, jh_string_span(jh_list_at(&sorted, i))));
  jh_hw_set(h, software, "definitions", jh_hw_root(definitions));
  const jb_json_tree_view_t macros =
      jh_json_field(program->config_root, "macros");
  for (size_t i = 0; i < jh_json_count(macros); ++i) {
    const jb_json_tree_view_t item = jh_json_item(macros, i);
    const char *name = jh_hw_text(h, jh_json_field(item, "name"));
    if (strncmp(name, "HAL_", 4) == 0 &&
        !jh_hw_is(jh_json_field(item, "source"), "predefined") &&
        !jh_hw_bool(jh_json_field(item, "functionLike")) &&
        !jh_feature_symbol(jh_span(name), false))
      jh_hw_set(h, tunables, name, jh_json_field(item, "body"));
  }
  jh_hw_set(h, software, "tunables", jh_hw_root(tunables));
  jh_hw_set(h, software, "provider",
            jh_json_field(jh_json_field(target, "build"), "provider"));
  jh_hw_set(h, software, "abi", target_abi(h, options));
  jh_list_destroy(&sorted);
  jh_feature_resolution_destroy(&resolution);
  jh_list_destroy(&empty);
  jh_list_destroy(&normalized);
  jh_list_destroy(&required);
  jh_config_directives_destroy(&declarations);
  return jh_hw_root(software);
}

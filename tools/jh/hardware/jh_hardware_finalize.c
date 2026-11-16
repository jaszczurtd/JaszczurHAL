#include "jh_hardware_internal.h"
#include <stdlib.h>
#include <string.h>

const char *const jh_hardware_output_names[JH_HARDWARE_OUTPUT_COUNT] = {
    "jh_board_config.h",
    "jh_hardware_config.cmake",
    "jh_hardware_package.cmake",
    "jh_link_contract.h",
    "jh_link_contract_definition.c",
    "jh_link_contract_reference.c",
    "jh_hardware_config.json",
    "generation.d"};

void jh_hardware_outputs_init(jh_hardware_outputs_t *outputs) {
  memset(outputs, 0, sizeof *outputs);
  for (size_t i = 0; i < COUNTOF(outputs->content); ++i)
    (void)jb_string_init(&outputs->content[i], jb_heap_allocator());
}

void jh_hardware_outputs_destroy(jh_hardware_outputs_t *outputs) {
  if (outputs != NULL)
    for (size_t i = 0; i < COUNTOF(outputs->content); ++i)
      jb_string_destroy(&outputs->content[i]);
}

static void cmake_set(jh_hw_t *h, jb_string_t *out, const char *name,
                      const char *value) {
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jh_cmake_assignment(out, jh_span(name), jh_span(value)));
  jh_put(out, &h->memory.status, "\n");
}

static const char *join_strings(jh_hw_t *h, const jb_vector_t *list) {
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  for (size_t i = 0; i < jb_vector_size(list); ++i) {
    if (i != 0)
      jh_put(&text, &h->memory.status, ";");
    jh_put_span(&text, &h->memory.status, jh_string_span(jh_list_at(list, i)));
  }
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

static const char *join_array(jh_hw_t *h, jb_json_tree_view_t values) {
  jb_vector_t list = {0};
  jh_node_storage(&h->memory, jh_list_init(&list));
  for (size_t i = 0; i < jh_json_count(values); ++i)
    jh_node_storage(
        &h->memory,
        jh_list_push(&list, jh_span(jh_hw_text(h, jh_json_item(values, i)))));
  const char *result = join_strings(h, &list);
  jh_list_destroy(&list);
  return result;
}

static void object_entries(jh_hw_t *h, jb_json_tree_view_t value,
                           const char *prefix, bool sorted, jb_vector_t *out) {
  jb_vector_t keys = {0};
  jh_node_storage(&h->memory, jh_list_init(&keys));
  if (sorted)
    jh_hw_keys(h, value, &keys);
  else
    for (size_t i = 0; i < jh_json_count(value); ++i)
      jh_node_storage(&h->memory,
                      jh_list_push(&keys, jh_json_key(jh_json_item(value, i))));
  for (size_t i = 0; i < jb_vector_size(&keys); ++i) {
    const char *name = jb_string_data(jh_list_at(&keys, i));
    const jb_json_tree_view_t item = jh_json_field(value, name);
    if (jh_json_type(item) == JB_JSON_OBJECT)
      continue;
    const char *text = jh_json_type(item) == JB_JSON_STRING
                           ? jh_hw_text(h, item)
                           : jh_hw_repr(h, item);
    jh_node_storage(
        &h->memory,
        jh_list_push_parts(out, JH_PARTS(jh_span(prefix), jh_span(name),
                                         jh_span("="), jh_span(text))));
  }
  jh_list_destroy(&keys);
}

static void identity_record(jh_hw_t *h, const jh_project_build_t *program,
                            jb_json_tree_view_t document,
                            jb_json_tree_view_t software, const char *digest,
                            jb_string_t *out) {
  const jb_json_tree_view_t selection = jh_json_field(document, "selection");
  cmake_set(h, out, "JH_TARGET_SELECTOR",
            jh_hw_text(h, jh_json_field(jh_json_field(h->target->root, "hal"),
                                        "targetSelector")));
  cmake_set(h, out, "JH_HARDWARE_SHA256",
            jh_hw_text(h, jh_json_field(document, "hardwareSha256")));
  cmake_set(h, out, "JH_CONFIG_SHA256", digest);
  cmake_set(h, out, "JH_ASSEMBLY",
            jh_hw_text(h, jh_json_field(selection, "assembly")));
  cmake_set(h, out, "JH_PROJECT_VARIANT",
            jh_hw_text(h, jh_json_field(software, "variant")));
  cmake_set(h, out, "JH_PROJECT_FEATURES",
            join_array(h, jh_json_field(software, "requestedFeatures")));
  cmake_set(h, out, "JH_PROJECT_DEFINITIONS",
            join_strings(h, &program->definitions));
  jb_vector_t entries = {0};
  jh_node_storage(&h->memory, jh_list_init(&entries));
  object_entries(h, jh_json_field(software, "tunables"), "", true, &entries);
  cmake_set(h, out, "JH_PROJECT_TUNABLES", join_strings(h, &entries));
  jh_list_destroy(&entries);
  jh_node_storage(&h->memory, jh_list_init(&entries));
  const jb_json_tree_view_t abi = jh_json_field(software, "abi");
  object_entries(h, abi, "", false, &entries);
  object_entries(h, jh_json_field(abi, "compiler"), "", true, &entries);
  object_entries(h, jh_json_field(abi, "layout"), "layout.", false, &entries);
  cmake_set(h, out, "JH_HARDWARE_ABI", join_strings(h, &entries));
  jh_list_destroy(&entries);
}

static void
cmake_records(jh_hw_t *h, const jh_hardware_build_options_t *options,
              const jh_project_build_t *program, jb_json_tree_view_t document,
              jb_json_tree_view_t software, const char *digest,
              const jb_string_t *board, jh_hardware_outputs_t *outputs) {
  jb_string_t identity = {0};
  (void)jb_string_init(&identity, jb_heap_allocator());
  identity_record(h, program, document, software, digest, &identity);
  const char *body = strchr(jb_string_data(board), '\n');
  body = body != NULL ? body + 1 : "";
  const char *banner = "# Generated by jh hardware finalize; do not edit.\n";
  jb_string_t *build = &outputs->content[JH_HARDWARE_CMAKE],
              *package = &outputs->content[JH_HARDWARE_PACKAGE];
  jh_put(build, &h->memory.status, banner);
  jh_put(build, &h->memory.status, body);
  cmake_set(h, build, "JH_HARDWARE_HEADER", options->header_file);
  cmake_set(h, build, "JH_HARDWARE_RESOLVED_FILE", options->hardware_file);
  cmake_set(
      h, build, "JH_HARDWARE_CONFIG_FILE",
      jh_hw_path(h, options->output_directory, "jh_hardware_config.json"));
  jh_put_span(build, &h->memory.status, jh_string_span(&identity));
  jh_put(package, &h->memory.status, banner);
  jh_put(package, &h->memory.status,
         "# The configuration of an installed JaszczurHAL package; paths "
         "follow this file.\n"
         "get_filename_component(_jh_package_prefix "
         "\"${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n");
  for (const char *line = body; *line != '\0';) {
    const char *end = strchr(line, '\n');
    const size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
    if (strncmp(line, "set(JH_BOARD_INPUT_GLOBS ",
                strlen("set(JH_BOARD_INPUT_GLOBS ")) != 0) {
      jh_put_span(package, &h->memory.status, (jb_json_span_t){line, length});
      jh_put(package, &h->memory.status, "\n");
    }
    line += length + (end != NULL ? 1u : 0u);
  }
  jh_put(package, &h->memory.status,
         "set(JH_HAL_PACKAGE_PREFIX \"${_jh_package_prefix}\")\n"
         "set(JH_HAL_PACKAGE_LIBRARY "
         "\"${_jh_package_prefix}/lib/libJaszczurHAL.a\")\n"
         "set(JH_HAL_PACKAGE_INCLUDE_DIRS "
         "\"${_jh_package_prefix}/include;${_jh_package_prefix}/include/"
         "generated\")\n"
         "set(JH_HAL_PACKAGE_REFERENCE_SOURCE "
         "\"${_jh_package_prefix}/share/JaszczurHAL/generated/"
         "jh_link_contract_reference.c\")\n"
         "set(JH_HARDWARE_HEADER "
         "\"${_jh_package_prefix}/include/generated/jh_hardware.h\")\n"
         "set(JH_HARDWARE_RESOLVED_FILE "
         "\"${_jh_package_prefix}/share/JaszczurHAL/generated/"
         "jh_hardware_resolved.json\")\n"
         "set(JH_HARDWARE_CONFIG_FILE "
         "\"${_jh_package_prefix}/share/JaszczurHAL/generated/"
         "jh_hardware_config.json\")\n");
  jh_put_span(package, &h->memory.status, jh_string_span(&identity));
  jh_put(package, &h->memory.status, "unset(_jh_package_prefix)\n");
  jb_string_destroy(&identity);
}

static void final_dependencies(jh_hw_t *h,
                               const jh_hardware_build_options_t *options,
                               jb_json_tree_view_t document,
                               const jh_project_build_t *program,
                               jb_vector_t *out) {
  jh_node_storage(&h->memory,
                  jh_list_push(out, jh_span(options->hardware_file)));
  jh_node_storage(&h->memory, jh_list_push(out, jh_span(options->header_file)));
  const jb_json_tree_view_t files =
      jh_json_field(program->config_root, "files");
  for (size_t i = 0; i < jh_json_count(files); ++i)
    jh_node_storage(
        &h->memory,
        jh_list_push(out, jh_span(jh_hw_text(h, jh_json_item(files, i)))));
  const jb_json_tree_view_t dependencies =
      jh_json_field(document, "dependencies");
  for (size_t i = 0; i < jh_json_count(dependencies); ++i) {
    const char *name =
        jh_hw_text(h, jh_json_field(jh_json_item(dependencies, i), "path"));
    const bool hal = strncmp(name, "hal:", 4) == 0;
    jh_node_storage(
        &h->memory,
        jh_list_push(out,
                     jh_span(jh_hw_path(h,
                                        hal ? jb_string_data(&h->registry->root)
                                            : options->config_directory,
                                        hal ? name + 4 : name))));
  }
}

static void header_prelude(jh_hw_t *h, const char *hash,
                           jb_json_tree_view_t software, jb_vector_t *lines) {
  const char *identity = jh_hw_join(
      h, JH_PARTS(jh_span("0x"), ((jb_json_span_t){hash, 16}), jh_span("ULL")));
  static const char *const start[] = {
      "#ifndef JH_HW_SCHEMA_VERSION",
      "#error \"jh_hardware.h must be loaded before jh_board_config.h\"",
      "#endif"};
  for (size_t i = 0; i < COUNTOF(start); ++i)
    jh_node_storage(&h->memory, jh_list_push(lines, jh_span(start[i])));
  jh_node_storage(
      &h->memory,
      jh_list_push_parts(lines, JH_PARTS(jh_span("#if JH_HW_HARDWARE_ID != "),
                                         jh_span(identity))));
  jh_node_storage(
      &h->memory,
      jh_list_push(lines,
                   jh_span("#error \"jh_board_config.h and jh_hardware.h come "
                           "from different generations\"")));
  jh_node_storage(&h->memory, jh_list_push(lines, jh_span("#endif")));
  jh_node_storage(&h->memory,
                  jh_list_push_parts(
                      lines, JH_PARTS(jh_span("#define HAL_BOARD_HARDWARE_ID "),
                                      jh_span(identity))));
  const jb_json_tree_view_t layout =
      jh_json_field(jh_json_field(software, "abi"), "layout");
  if (layout.tree != NULL)
    jh_hw_layout_checks(h, layout, lines);
}

hal_status_t jh_hardware_finalize(const jh_hardware_build_options_t *options,
                                  jh_hardware_outputs_t *outputs,
                                  jh_failure_t *failure) {
  if (options == NULL || outputs == NULL || failure == NULL ||
      options->registry == NULL || options->features == NULL ||
      options->config_directory == NULL || options->hardware_file == NULL ||
      options->header_file == NULL || options->output_directory == NULL)
    return jh_fail_arguments(failure);
  if (options->compiler == NULL && options->compiler_flags != NULL &&
      jb_vector_size(options->compiler_flags) != 0)
    return jh_fail_pointer(
        failure, "JH-HW-GENERATION", jh_span(""), jh_span(""),
        JH_PARTS(jh_span(
            "--compiler-flag needs --compiler (argument --compiler-flag)")));
  jh_hw_t *h = calloc(1, sizeof *h);
  if (h == NULL)
    return jh_fail_storage(failure, HAL_ENOMEM);
  jh_hardware_t owner = {h};
  jh_hw_init(h, options->registry, failure);
  const jb_json_tree_view_t document =
      jh_hw_restore(h, options->hardware_file, options->header_file,
                    options->config_directory);
  jb_vector_t definitions = {0}, components = {0}, cmake_definitions = {0},
              prelude = {0}, dependencies = {0};
  jb_vector_t *lists[] = {&definitions, &components, &cmake_definitions,
                          &prelude, &dependencies};
  for (size_t i = 0; i < COUNTOF(lists); ++i)
    jh_node_storage(&h->memory, jh_list_init(lists[i]));
  jh_project_build_t program;
  jh_project_build_init(&program);
  jh_board_outputs_t board_outputs = {0};
  jh_node_storage(&h->memory, jh_board_outputs_init(&board_outputs));
  jb_json_tree_view_t board = {0}, software = {0};
  if (jh_hw_phase(h) == HAL_OK)
    board = jh_hw_assembly_board(h, document, &definitions, &components);
  if (jh_hw_phase(h) == HAL_OK)
    software = jh_hw_program(h, options, document, &program);
  char digest[65] = {0};
  if (jh_hw_phase(h) == HAL_OK) {
    jb_json_tree_t *identity = jh_hw_object(h);
    jh_hw_set(h, identity, "hardwareSha256",
              jh_json_field(document, "hardwareSha256"));
    jh_hw_set(h, identity, "software", software);
    jh_hw_digest(h, jh_hw_root(identity), digest);
    for (size_t i = 0; i < jb_vector_size(&definitions); ++i) {
      const jb_json_span_t item = jh_string_span(jh_list_at(&definitions, i));
      const char *name = jh_hw_definition_name(h, item);
      if (!jh_hw_owned(name))
        jh_node_storage(&h->memory, jh_list_push(&cmake_definitions, item));
    }
    header_prelude(h, jh_hw_text(h, jh_json_field(document, "hardwareSha256")),
                   software, &prelude);
    final_dependencies(h, options, document, &program, &dependencies);
    const jh_board_overrides_t overrides = {&definitions, &cmake_definitions,
                                            &components,  jh_span(digest),
                                            &prelude,     &dependencies};
    const jh_board_build_t build = {&h->registry->boards,
                                    options->features,
                                    h->target->root,
                                    board,
                                    &program.requested,
                                    jb_string_data(&h->registry->root),
                                    &overrides};
    if (h->memory.status == HAL_OK)
      h->memory.status =
          jh_board_build_outputs(&build, &board_outputs, failure);
  }
  if (jh_hw_phase(h) == HAL_OK) {
    static const struct {
      size_t from, to;
    } copies[] = {
        {JH_BOARD_OUTPUT_HEADER, JH_HARDWARE_BOARD_HEADER},
        {JH_BOARD_OUTPUT_LINK_HEADER, JH_HARDWARE_LINK_HEADER},
        {JH_BOARD_OUTPUT_LINK_DEFINITION, JH_HARDWARE_LINK_DEFINITION},
        {JH_BOARD_OUTPUT_LINK_REFERENCE, JH_HARDWARE_LINK_REFERENCE},
        {JH_BOARD_OUTPUT_DEPENDENCIES, JH_HARDWARE_DEPENDENCIES}};
    for (size_t i = 0; i < COUNTOF(copies); ++i) {
      const jb_string_t *input = &board_outputs.content[copies[i].from];
      jb_string_t *output = &outputs->content[copies[i].to];
      const bool link = copies[i].from == JH_BOARD_OUTPUT_LINK_DEFINITION ||
                        copies[i].from == JH_BOARD_OUTPUT_LINK_REFERENCE;
      if (options->compiler != NULL && link) {
        const char *newline = strchr(jb_string_data(input), '\n');
        const size_t first = newline != NULL
                                 ? (size_t)(newline + 1 - jb_string_data(input))
                                 : jb_string_size(input);
        jh_put_span(output, &h->memory.status,
                    (jb_json_span_t){jb_string_data(input), first});
        jh_hw_abi_checks(
            h, jh_json_field(jh_json_field(software, "abi"), "compiler"),
            output);
        jh_put_span(output, &h->memory.status,
                    (jb_json_span_t){jb_string_data(input) + first,
                                     jb_string_size(input) - first});
      } else {
        jh_put_span(output, &h->memory.status, jh_string_span(input));
      }
    }
    cmake_records(h, options, &program, document, software, digest,
                  &board_outputs.content[JH_BOARD_OUTPUT_CMAKE], outputs);
    jb_json_tree_t *final = jh_hw_copy(h, document);
    jh_hw_set(h, final, "software", software);
    jh_hw_set(h, final, "configSha256", jh_hw_string(h, jh_span(digest)));
    const jb_json_tree_view_t serialized =
        jh_hw_serialized(h, jh_hw_root(final), false);
    if (h->memory.status == HAL_OK)
      jh_node_storage(
          &h->memory,
          jh_hw_format(serialized, 2, &outputs->content[JH_HARDWARE_CONFIG]));
  }
  const hal_status_t status = jh_hw_phase(h);
  jh_board_outputs_destroy(&board_outputs);
  jh_project_build_destroy(&program);
  for (size_t i = 0; i < COUNTOF(lists); ++i)
    jh_list_destroy(lists[i]);
  jh_hardware_destroy(&owner);
  return status;
}

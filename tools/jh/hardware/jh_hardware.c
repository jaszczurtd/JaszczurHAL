#include "jh_hardware_internal.h"
#include <stdlib.h>
#include <string.h>

void jh_hardware_registry_init(jh_hardware_registry_t *registry) {
  memset(registry, 0, sizeof *registry);
  (void)jb_string_init(&registry->root, jb_heap_allocator());
  (void)jb_string_init(&registry->schema_dir, jb_heap_allocator());
  jh_board_registry_init(&registry->boards);
  jh_clock_registry_init(&registry->clocks);
  (void)jh_catalogue_init(&registry->catalogue);
  jh_feature_registry_init(&registry->feature_registry);
  (void)jh_list_init(&registry->features);
  (void)jh_list_init(&registry->capabilities);
}

void jh_hardware_registry_destroy(jh_hardware_registry_t *registry) {
  if (registry == NULL)
    return;
  jh_list_destroy(&registry->capabilities);
  jh_list_destroy(&registry->features);
  jh_catalogue_destroy(&registry->catalogue);
  jh_feature_registry_destroy(&registry->feature_registry);
  jh_clock_registry_destroy(&registry->clocks);
  jh_board_registry_destroy(&registry->boards);
  jb_string_destroy(&registry->schema_dir);
  jb_string_destroy(&registry->root);
  memset(registry, 0, sizeof *registry);
}

hal_status_t jh_hardware_registry_load(jh_hardware_registry_t *registry,
                                       const char *root, size_t max_bytes,
                                       jh_failure_t *failure) {
  if (registry == NULL || root == NULL || failure == NULL || max_bytes == 0)
    return jh_fail_arguments(failure);
  registry->max_bytes = max_bytes;
  jb_string_t path = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  jh_json_location_t where;
  jh_json_location_init(&where);
  hal_status_t status = jh_python_resolve(root, NULL, &registry->root, failure);
  if (status == HAL_OK)
    status = jh_path_join(jb_string_data(&registry->root), "config/hardware",
                          &registry->schema_dir);
  if (status == HAL_OK)
    status = jh_path_join(jb_string_data(&registry->root), "boards", &path);
  if (status == HAL_OK)
    status = jh_board_registry_load_with_features(
        &registry->boards, jb_string_data(&registry->root),
        jb_string_data(&path), max_bytes, &registry->feature_registry, failure);
  if (status == HAL_OK)
    status =
        jh_path_join(jb_string_data(&registry->schema_dir), "clocks", &path);
  if (status == HAL_OK)
    status = jh_clock_registry_load(&registry->clocks,
                                    jb_string_data(&registry->root),
                                    jb_string_data(&path), max_bytes, failure);
  if (status == HAL_OK)
    status =
        jh_path_join(jb_string_data(&registry->schema_dir), "bindings", &path);
  if (status == HAL_OK)
    status = jh_catalogue_load(&registry->catalogue, &where,
                               jb_string_data(&registry->schema_dir),
                               jb_string_data(&path), max_bytes, failure);
  if (status == HAL_OK && registry->feature_registry.digest[0] == '\0') {
    status = jh_path_join(jb_string_data(&registry->root), "config", &path);
    if (status == HAL_OK)
      status =
          jh_feature_registry_load(&registry->feature_registry,
                                   jb_string_data(&path), max_bytes, failure);
  }
  for (size_t i = 0; status == HAL_OK &&
                     i < jb_vector_size(&registry->feature_registry.features);
       ++i) {
    const jh_feature_t *feature =
        jb_vector_const_at(&registry->feature_registry.features, i);
    status = jh_list_push(&registry->features, jh_string_span(&feature->name));
  }
  for (size_t i = 0;
       status == HAL_OK && i < jh_json_count(registry->boards.capabilities);
       ++i)
    status = jh_list_push(
        &registry->capabilities,
        jh_json_key(jh_json_item(registry->boards.capabilities, i)));
  if (status == HAL_OK)
    status = jh_catalogue_check(&registry->catalogue, &registry->features,
                                &registry->capabilities, failure);
  if (status != HAL_OK && failure->status == HAL_OK)
    status = jh_fail_storage(failure, status);
  jh_json_location_destroy(&where);
  jb_string_destroy(&path);
  return status;
}

void jh_hw_init(jh_hw_t *h, const jh_hardware_registry_t *registry,
                jh_failure_t *failure) {
  h->registry = registry;
  h->failure = failure;
  jh_failure_init(&h->first);
  jh_node_reader_init(&h->memory, failure);
  jh_json_location_init(&h->where);
  jh_node_storage(&h->memory, jh_catalogue_init(&h->project_types));
  jh_node_storage(
      &h->memory,
      jb_vector_init(&h->trees, sizeof(jb_json_tree_t *), jb_heap_allocator()));
  jh_node_storage(&h->memory, jb_vector_init(&h->types, sizeof(jh_hw_type_t),
                                             jb_heap_allocator()));
  jh_node_storage(&h->memory, jb_vector_init(&h->nodes, sizeof(jh_hw_node_t *),
                                             jb_heap_allocator()));
  jh_node_storage(&h->memory,
                  jb_map_init(&h->claims, sizeof(jh_hw_endpoint_key_t),
                              sizeof(jh_hw_claim_t), jh_hw_endpoint_order, NULL,
                              jb_heap_allocator()));
  jh_node_storage(
      &h->memory,
      jb_vector_init(&h->net_states, sizeof(jh_hw_net_t), jb_heap_allocator()));
  jh_node_storage(&h->memory,
                  jb_map_init(&h->connections, sizeof(jb_json_span_t),
                              sizeof(const char *), jh_catalogue_span_order,
                              NULL, jb_heap_allocator()));
  jh_node_storage(&h->memory,
                  jb_map_init(&h->module_owners, sizeof(jb_json_span_t),
                              sizeof(const char *), jh_catalogue_span_order,
                              NULL, jb_heap_allocator()));
}

void jh_hardware_destroy(jh_hardware_t *hardware) {
  if (hardware == NULL || hardware->state == NULL)
    return;
  jh_hw_t *h = hardware->state;
  for (size_t i = 0; i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    jb_vector_destroy(&node->pins);
    jb_vector_destroy(&node->properties);
    free(node);
  }
  jb_map_destroy(&h->claims);
  jb_vector_destroy(&h->net_states);
  jb_map_destroy(&h->connections);
  jb_map_destroy(&h->module_owners);
  jb_vector_destroy(&h->nodes);
  jb_vector_destroy(&h->types);
  for (size_t i = 0; i < jb_vector_size(&h->trees); ++i)
    jb_json_tree_destroy(*(jb_json_tree_t **)jb_vector_at(&h->trees, i));
  jb_vector_destroy(&h->trees);
  jh_catalogue_destroy(&h->project_types);
  jh_json_document_destroy(&h->document);
  jh_json_location_destroy(&h->where);
  jh_node_reader_destroy(&h->memory);
  jh_failure_destroy(&h->first);
  free(h);
  hardware->state = NULL;
}

static void schema_error(jh_hw_t *h, const jh_schema_files_t *schemas) {
  const jh_schema_report_t *first = NULL;
  for (size_t i = 0; i < jh_schema_files_report_count(schemas); ++i) {
    const jh_schema_report_t *next = jh_schema_files_report(schemas, i);
    if (first == NULL ||
        jh_json_pointer_compare(jh_string_span(&next->pointer),
                                jh_string_span(&first->pointer)) < 0)
      first = next;
  }
  h->memory.status = jh_fail_pointer(
      h->failure, "JH-HW-SCHEMA", jh_span(h->path),
      first == NULL ? jh_span("") : jh_string_span(&first->pointer),
      JH_PARTS(first == NULL
                   ? jh_span("the device tree schema rejects this file")
                   : jh_string_span(&first->message)));
}

hal_status_t jh_hardware_load(jh_hardware_t *hardware,
                              const jh_hardware_registry_t *registry,
                              const char *device_tree_path,
                              jh_failure_t *failure) {
  if (hardware == NULL || hardware->state != NULL || registry == NULL ||
      device_tree_path == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = calloc(1, sizeof *h);
  if (h == NULL)
    return jh_fail_storage(failure, HAL_ENOMEM);
  hardware->state = h;
  jh_hw_init(h, registry, failure);
  jb_string_t path = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  if (h->memory.status == HAL_OK)
    h->memory.status =
        jh_python_resolve(device_tree_path, NULL, &path, failure);
  h->path = jh_hw_join(h, JH_PARTS(jh_string_span(&path)));
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_path_parent(h->path, &path, failure);
  h->base = jh_hw_join(h, JH_PARTS(jh_string_span(&path)));
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_json_read_document(
        &h->where, h->path, registry->max_bytes, true, &h->document, failure);
  if (h->memory.status != HAL_OK && failure->exit_code != 4)
    h->memory.status =
        jh_fail_pointer(failure, "JH-HW-SCHEMA", jh_span(h->path), jh_span(""),
                        JH_PARTS(jh_string_span(&failure->message)));
  if (h->where.located && h->memory.status != HAL_OK) {
    failure->line = h->where.line;
    failure->column = h->where.column;
  }
  jh_schema_files_t schemas;
  jh_schema_files_init(&schemas, &h->where, registry->max_bytes);
  jh_node_storage(&h->memory,
                  jh_path_join(jb_string_data(&registry->schema_dir),
                               "device_tree.schema.json", &path));
  if (h->memory.status == HAL_OK)
    h->memory.status =
        jh_schema_files_root(&schemas, jb_string_data(&path), NULL, failure);
  if (h->memory.status == HAL_OK) {
    h->memory.status =
        jh_schema_files_validate_sized(&schemas, &h->document, failure);
    if (h->memory.status == HAL_ECONFIG)
      schema_error(h, &schemas);
    else if (h->memory.status == HAL_EOVERFLOW)
      h->memory.status = jh_fail_coded(
          failure, HAL_EOVERFLOW, 4, "JH-HW-SCHEMA", jh_span(h->path),
          JH_PARTS(jh_string_span(&failure->message)));
  }
  jb_json_node_t root = {0};
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_root(&h->document.doc, &root));
  if (h->memory.status == HAL_OK)
    h->data = jh_hw_import(h, root);
  jh_schema_files_destroy(&schemas);
  jb_string_destroy(&path);
  return h->memory.status;
}

static void load_types(jh_hw_t *h) {
  const jh_hardware_registry_t *registry = h->registry;
  jb_json_node_t root = {0}, bindings = {0};
  jh_node_storage(&h->memory, jb_json_root(&h->document.doc, &root));
  jh_node_storage(&h->memory,
                  jh_catalogue_borrow(&h->project_types, &registry->catalogue));
  if (h->memory.status == HAL_OK &&
      jb_json_find(root, jh_span("bindings"), &bindings) == HAL_OK)
    h->memory.status = jh_catalogue_load_project(
        &h->project_types, &h->where, jb_string_data(&registry->schema_dir),
        h->base, h->path, bindings, registry->max_bytes, h->failure);
  if (h->memory.status == HAL_OK)
    h->memory.status =
        jh_catalogue_check(&h->project_types, NULL, NULL, h->failure);
  if (h->memory.status == HAL_OK)
    h->memory.status =
        jh_catalogue_check(&h->project_types, &registry->features,
                           &registry->capabilities, h->failure);
  for (size_t i = 0; h->memory.status == HAL_OK &&
                     i < jb_vector_size(&h->project_types.bindings);
       ++i) {
    const jh_binding_t *binding = jh_catalogue_at(&h->project_types, i);
    const jh_hw_type_t type = {
        binding, jh_hw_import(h, jh_binding_root(&h->memory, binding))};
    jh_node_storage(&h->memory, jb_vector_append(&h->types, &type, 1));
  }
  if (h->memory.status == HAL_OK)
    jh_hw_declarations(h);
}

hal_status_t jh_hardware_list(const jh_hardware_t *hardware, jb_string_t *out,
                              jh_failure_t *failure) {
  if (hardware == NULL || hardware->state == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = hardware->state;
  h->failure = h->memory.failure = failure;
  jb_json_tree_t *list = jh_hw_array(h);
  const jb_json_tree_view_t assemblies = jh_json_field(h->data, "assemblies");
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  jh_hw_keys(h, assemblies, &names);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    const jb_json_tree_view_t item = jh_json_field(assemblies, name);
    jb_json_tree_t *entry = jh_hw_object(h);
    jh_hw_set(h, entry, "name", jh_hw_string(h, jh_span(name)));
    jh_hw_set(h, entry, "target", jh_json_field(item, "target"));
    jh_hw_set(h, entry, "board", jh_json_field(item, "board"));
    jh_hw_append(h, list, jh_hw_root(entry));
  }
  jh_list_destroy(&names);
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jh_hw_format(jh_hw_root(list), 2, out));
  return h->memory.status;
}

bool jh_hw_list_has(jb_json_tree_view_t list, const char *text) {
  for (size_t i = 0; i < jh_json_count(list); ++i)
    if (jh_hw_is(jh_json_item(list, i), text))
      return true;
  return false;
}

static void select_assembly(jh_hw_t *h, const char *target,
                            const char *assembly, bool boards) {
  const jb_json_tree_view_t assemblies = jh_json_field(h->data, "assemblies");
  jb_vector_t all = {0}, matches = {0};
  jh_node_storage(&h->memory, jh_list_init(&all));
  jh_node_storage(&h->memory, jh_list_init(&matches));
  jh_hw_keys(h, assemblies, &all);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&all);
       ++i) {
    const jb_string_t *name = jh_list_at(&all, i);
    const jb_json_tree_view_t entry =
        jh_json_field(assemblies, jb_string_data(name));
    if (target == NULL || jh_hw_is(jh_json_field(entry, "target"), target))
      jh_node_storage(&h->memory, jh_list_push(&matches, jh_string_span(name)));
  }
  const char *argument = NULL;
  jb_string_t message = {0};
  (void)jb_string_init(&message, jb_heap_allocator());
  if (assembly != NULL) {
    h->assembly = jh_json_field(assemblies, assembly);
    if (h->assembly.tree == NULL) {
      jh_put(&message, &h->memory.status, "unknown assembly ");
      jh_node_storage(&h->memory,
                      jh_python_repr_append(&message, jh_span(assembly)));
      jh_put(&message, &h->memory.status, "; known: ");
      jh_node_storage(&h->memory, jh_repr_list(&message, &all));
      argument = "--assembly";
    } else if (target != NULL &&
               !jh_hw_is(jh_json_field(h->assembly, "target"), target)) {
      jh_node_storage(
          &h->memory,
          jh_join(
              &message,
              JH_PARTS(
                  jh_span("assembly "), jh_span(assembly), jh_span(" is for "),
                  jh_span(jh_hw_text(h, jh_json_field(h->assembly, "target"))),
                  jh_span(", not "), jh_span(target))));
      argument = "--target";
    }
  } else if (jb_vector_size(&matches) == 1) {
    assembly = jb_string_data(jh_list_at(&matches, 0));
    h->assembly = jh_json_field(assemblies, assembly);
  } else if (jb_vector_size(&matches) == 0) {
    jh_node_storage(
        &h->memory,
        jh_join(&message, JH_PARTS(jh_span("no assembly for target "),
                                   jh_span(target == NULL ? "None" : target))));
    argument = "--target";
  } else {
    jh_put(&message, &h->memory.status, "choose one of the assemblies ");
    jh_node_storage(&h->memory, jh_repr_list(&message, &matches));
    jh_put(&message, &h->memory.status, " with --assembly");
    argument = target != NULL ? "--assembly" : "--target";
  }
  if (argument != NULL) {
    jh_hw_error(h, "JH-HW-ASSEMBLY", (jh_hw_origin_t){h->path, ""}, NULL,
                JH_PARTS(jh_string_span(&message), jh_span(" (argument "),
                         jh_span(argument), jh_span(")")));
    jh_node_storage(&h->memory, jh_append(&h->first.argument, argument));
  } else if (h->memory.status == HAL_OK) {
    h->assembly_name = jh_hw_join(h, JH_PARTS(jh_span(assembly)));
  }
  if (boards && h->memory.status == HAL_OK && h->assembly_name != NULL) {
    const char *board = jh_hw_text(h, jh_json_field(h->assembly, "board"));
    const char *id = jh_hw_text(h, jh_json_field(h->assembly, "target"));
    const char *at = jh_hw_pointer(h, "/assemblies", h->assembly_name);
    h->board =
        jh_board_registry_find(&h->registry->boards.boards, jh_span(board));
    h->target =
        jh_board_registry_find(&h->registry->boards.targets, jh_span(id));
    if (h->board == NULL)
      jh_hw_error(h, "JH-HW-BOARD",
                  (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "board")},
                  NULL, JH_PARTS(jh_span("unknown board "), jh_span(board)));
    else if (h->target == NULL ||
             !jh_hw_list_has(jh_json_field(h->board->root, "compatibleTargets"),
                             id))
      jh_hw_error(
          h, "JH-HW-TARGET-BOARD",
          (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "target")}, NULL,
          JH_PARTS(jh_span(board), jh_span(" supports "),
                   jh_span(jh_hw_repr(
                       h, jh_json_field(h->board->root, "compatibleTargets"))),
                   jh_span(", not "), jh_span(id)));
  }
  jb_string_destroy(&message);
  jh_list_destroy(&matches);
  jh_list_destroy(&all);
}

hal_status_t jh_hardware_select(jh_hardware_t *hardware, const char *target,
                                const char *assembly, jb_string_t *out,
                                jh_failure_t *failure) {
  if (hardware == NULL || hardware->state == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = hardware->state;
  h->failure = h->memory.failure = failure;
  if (h->memory.status != HAL_OK || h->assembly_name != NULL)
    return jh_fail_text(failure, HAL_ESTATE, 3,
                        "hardware selection requires a freshly loaded project");
  select_assembly(h, target, assembly, false);
  if (jh_hw_phase(h) == HAL_OK) {
    jb_json_tree_t *result = jh_hw_object(h);
    jh_hw_set(h, result, "name", jh_hw_string(h, jh_span(h->assembly_name)));
    jh_hw_set(h, result, "target", jh_json_field(h->assembly, "target"));
    jh_hw_set(h, result, "board", jh_json_field(h->assembly, "board"));
    jh_node_storage(&h->memory, jh_hw_format(jh_hw_root(result), 2, out));
  }
  return h->memory.status;
}

hal_status_t jh_hardware_resolve(jh_hardware_t *hardware, const char *target,
                                 const char *assembly, jh_failure_t *failure) {
  if (hardware == NULL || hardware->state == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = hardware->state;
  if (h->memory.status != HAL_OK || h->assembly_name != NULL)
    return jh_fail_text(
        failure, HAL_ESTATE, 3,
        "hardware resolution requires a freshly loaded project");
  h->failure = h->memory.failure = failure;
  load_types(h);
  if (jh_hw_phase(h) != HAL_OK)
    return h->memory.status;
  select_assembly(h, target, assembly, true);
  static void (*const phases[])(jh_hw_t *) = {
      jh_hw_presence,   jh_hw_properties, jh_hw_clock,      jh_hw_wiring,
      jh_hw_status_led, jh_hw_providers,  jh_hw_build_model};
  for (size_t i = 0; i < COUNTOF(phases); ++i) {
    if (jh_hw_phase(h) != HAL_OK)
      return h->memory.status;
    phases[i](h);
  }
  return jh_hw_phase(h);
}

jb_json_tree_view_t jh_hardware_model(const jh_hardware_t *hardware) {
  const jh_hw_t *h = hardware == NULL ? NULL : hardware->state;
  return h == NULL || h->memory.status != HAL_OK ? (jb_json_tree_view_t){0}
                                                 : h->model;
}

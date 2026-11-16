#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "jh_effective_features.h"
#include "jh_hardware.h"
#include "jh_hardware_build.h"
#include "jh_hardware_internal.h"
#include "jh_json_check.h"
#include "jh_process_support.h"
#include "jh_project_config.h"
#include <jb/filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static size_t checks, failures;

static void expect(bool condition, const char *file, const char *detail) {
  ++checks;
  if (!condition) {
    ++failures;
    (void)fprintf(stderr, "%s: %s\n", file, detail);
  }
}

static bool text_is(jb_json_tree_view_t value, const char *text) {
  jb_json_span_t span = {0};
  return jh_json_string(value, &span) && jh_span_equal(span, jh_span(text));
}

static const char *text(jb_json_tree_view_t value, jb_string_t *out) {
  jb_json_span_t span = {0};
  if (!jh_json_string(value, &span))
    return NULL;
  if (jb_string_assign(out, span.data, span.length) != HAL_OK)
    return NULL;
  return jb_string_data(out);
}

static hal_status_t resolve(const jh_hardware_registry_t *registry,
                            const char *path, const char *assembly,
                            const char *target, jh_hardware_t *hardware,
                            jh_failure_t *failure) {
  hal_status_t status = jh_hardware_load(hardware, registry, path, failure);
  if (status == HAL_OK)
    status = jh_hardware_resolve(hardware, target, assembly, failure);
  return status;
}

static void specification(const jh_hardware_registry_t *registry,
                          const char *fixtures, jb_json_tree_view_t cases) {
  jb_string_t relative = {0}, path = {0}, assembly = {0};
  (void)jb_string_init(&relative, jb_heap_allocator());
  (void)jb_string_init(&path, jb_heap_allocator());
  (void)jb_string_init(&assembly, jb_heap_allocator());
  for (size_t i = 0; i < jh_json_count(cases); ++i) {
    const jb_json_tree_view_t test = jh_json_item(cases, i);
    if (!text_is(jh_json_field(test, "schema"), "device_tree.schema.json") ||
        text_is(jh_json_field(test, "phase"), "finalize"))
      continue;
    const char *file = text(jh_json_field(test, "file"), &relative);
    if (file == NULL || jh_path_join(fixtures, file, &path) != HAL_OK) {
      expect(false, "fixture", "invalid test path");
      continue;
    }
    const bool accept = text_is(jh_json_field(test, "expected"), "accept");
    const jb_json_tree_view_t assemblies =
        jh_json_field(test, "resolveAssemblies");
    const size_t count = accept ? jh_json_count(assemblies) : 1;
    for (size_t j = 0; j < count; ++j) {
      jh_failure_t failure;
      jh_failure_init(&failure);
      jh_hardware_t hardware = {0};
      const char *name =
          text(accept ? jh_json_item(assemblies, j)
                      : jh_json_field(jh_json_field(test, "input"), "assembly"),
               &assembly);
      const hal_status_t status = resolve(registry, jb_string_data(&path), name,
                                          NULL, &hardware, &failure);
      if (accept) {
        expect(status == HAL_OK, file, jb_string_data(&failure.message));
        expect(jh_json_type(jh_hardware_model(&hardware)) == JB_JSON_OBJECT,
               file, "resolved model");
        if (status == HAL_OK) {
          const jh_hw_t *h = hardware.state;
          size_t project_types = 0;
          for (size_t k = 0; k < jb_vector_size(&h->project_types.bindings);
               ++k)
            project_types +=
                strcmp(jh_catalogue_at(&h->project_types, k)->origin,
                       "project") == 0
                    ? 1u
                    : 0u;
          expect(project_types ==
                     jh_json_count(jh_json_field(h->data, "bindings")),
                 file, "all declared project types loaded");
        }
      } else {
        expect(status == HAL_ECONFIG, file, jb_string_data(&failure.message));
        expect(text_is(jh_json_field(test, "error"),
                       jb_string_data(&failure.code)),
               file, jb_string_data(&failure.message));
        expect(text_is(jh_json_field(test, "at"),
                       jb_string_data(&failure.pointer)),
               file, jb_string_data(&failure.message));
        expect(strcmp(jb_string_data(&failure.source), jb_string_data(&path)) ==
                   0,
               file, "diagnostic source");
        const jb_json_tree_view_t related = jh_json_field(test, "related");
        if (jh_json_count(related) != 0) {
          jb_string_t expected = {0};
          (void)jb_string_init(&expected, jb_heap_allocator());
          (void)jh_append(&expected, " (earlier: ");
          for (size_t k = 0; k < jh_json_count(related); ++k) {
            jb_json_span_t pointer = {0};
            (void)jh_json_string(jh_json_item(related, k), &pointer);
            if (k != 0)
              (void)jh_append(&expected, ", ");
            (void)jh_append_span(&expected, pointer);
          }
          (void)jh_append(&expected, ")");
          expect(strstr(jb_string_data(&failure.message),
                        jb_string_data(&expected)) != NULL,
                 file, jb_string_data(&failure.message));
          jb_string_destroy(&expected);
        }
      }
      jh_hardware_destroy(&hardware);
      jh_failure_destroy(&failure);
    }
  }
  jb_string_destroy(&assembly);
  jb_string_destroy(&path);
  jb_string_destroy(&relative);
}

static void selection_cases(const jh_hardware_registry_t *registry,
                            const char *fixtures, jb_json_tree_view_t cases) {
  for (size_t i = 0; i < jh_json_count(cases); ++i) {
    const jb_json_tree_view_t test = jh_json_item(cases, i),
                              input = jh_json_field(test, "input");
    jb_string_t path = {0}, name = {0}, target = {0}, selected = {0},
                result = {0};
    jb_string_t *strings[] = {&path, &name, &target, &selected, &result};
    for (size_t j = 0; j < COUNTOF(strings); ++j)
      (void)jb_string_init(strings[j], jb_heap_allocator());
    jh_failure_t failure;
    jh_failure_init(&failure);
    jh_hardware_t hardware = {0};
    const char *file = text(jh_json_field(test, "file"), &name);
    hal_status_t status =
        file != NULL ? jh_path_join(fixtures, file, &path) : HAL_EINVAL;
    if (status == HAL_OK)
      status = jh_hardware_load(&hardware, registry, jb_string_data(&path),
                                &failure);
    if (status == HAL_OK)
      status = jh_hardware_select(
          &hardware, text(jh_json_field(input, "target"), &target),
          text(jh_json_field(input, "assembly"), &selected), &result, &failure);
    if (jh_json_field(test, "error").tree != NULL) {
      expect(status == HAL_ECONFIG && text_is(jh_json_field(test, "error"),
                                              jb_string_data(&failure.code)),
             file, jb_string_data(&failure.message));
      expect(strstr(jb_string_data(&failure.message), " (argument --") != NULL,
             file, "selection argument");
    } else {
      expect(status == HAL_OK, file, jb_string_data(&failure.message));
      jb_json_tree_t *tree = NULL;
      expect(jh_json_tree_from_text(jh_string_span(&result), &tree) == HAL_OK,
             file, "selected JSON");
      jb_json_tree_view_t root = {0};
      (void)jb_json_tree_root(tree, &root);
      expect(text_is(jh_json_field(root, "name"),
                     text(jh_json_field(test, "assembly"), &selected)),
             file, "selected assembly");
      jb_json_tree_destroy(tree);
    }
    jh_hardware_destroy(&hardware);
    jh_failure_destroy(&failure);
    for (size_t j = 0; j < COUNTOF(strings); ++j)
      jb_string_destroy(strings[j]);
  }
}

static jb_json_tree_view_t member_path(jh_hw_t *h, jb_json_tree_view_t nodes,
                                       const char *key, const char *group) {
  const char *dot = strrchr(key, '.');
  if (dot == NULL)
    return (jb_json_tree_view_t){0};
  const char *path =
      jh_hw_join(h, JH_PARTS(((jb_json_span_t){key, (size_t)(dot - key)})));
  return jh_json_field(jh_json_field(jh_json_field(nodes, path), group),
                       dot + 1);
}

static void resolution_cases(const jh_hardware_registry_t *registry,
                             const char *fixtures, jb_json_tree_view_t cases) {
  for (size_t i = 0; i < jh_json_count(cases); ++i) {
    const jb_json_tree_view_t test = jh_json_item(cases, i);
    jb_string_t path = {0}, name = {0}, assembly = {0};
    (void)jb_string_init(&path, jb_heap_allocator());
    (void)jb_string_init(&name, jb_heap_allocator());
    (void)jb_string_init(&assembly, jb_heap_allocator());
    jh_failure_t failure;
    jh_failure_init(&failure);
    jh_hardware_t hardware = {0};
    const char *file = text(jh_json_field(test, "file"), &name);
    hal_status_t status = jh_path_join(fixtures, file, &path);
    if (status == HAL_OK)
      status = resolve(registry, jb_string_data(&path),
                       text(jh_json_field(test, "assembly"), &assembly), NULL,
                       &hardware, &failure);
    expect(status == HAL_OK, file, jb_string_data(&failure.message));
    jh_hw_t *h = hardware.state;
    if (status == HAL_OK) {
      const jb_json_tree_view_t model =
          jh_json_field(jh_hardware_model(&hardware), "hardware");
      const jb_json_tree_view_t nodes = jh_json_field(model, "nodes");
      static const char *const maps[] = {"properties", "owners", "transport",
                                         "statusLed", "providers"};
      for (size_t j = 0; j < COUNTOF(maps); ++j) {
        const jb_json_tree_view_t expected = jh_json_field(test, maps[j]);
        for (size_t k = 0; k < jh_json_count(expected); ++k) {
          const jb_json_tree_view_t value = jh_json_item(expected, k);
          const char *key = jh_hw_key(h, value);
          const jb_json_tree_view_t actual =
              j == 0   ? jh_json_field(member_path(h, nodes, key, "properties"),
                                       "value")
              : j == 1 ? jh_json_field(member_path(h, nodes, key, "signals"),
                                       "owner")
              : j == 2 ? jh_json_field(jh_json_field(nodes, key), "transport")
                       : jh_json_field(jh_json_field(model, maps[j]), key);
          expect(jh_hw_equal(h, actual, value), file, key);
        }
      }
      const jb_json_tree_view_t unconnected =
          jh_json_field(test, "unconnected");
      for (size_t j = 0; j < jh_json_count(unconnected); ++j) {
        const char *key = jh_hw_text(h, jh_json_item(unconnected, j));
        expect(jh_json_type(jh_json_field(member_path(h, nodes, key, "signals"),
                                          "endpoint")) == JB_JSON_NULL,
               file, key);
      }
      const jb_json_tree_view_t no_init =
          jh_json_field(test, "noDrivingInitializer");
      for (size_t j = 0; j < jh_json_count(no_init); ++j) {
        const char *key = jh_hw_text(h, jh_json_item(no_init, j));
        const jb_json_tree_view_t node = jh_json_field(nodes, key),
                                  signals = jh_json_field(node, "signals");
        expect(jh_hw_initializer(h, key, node, model).c == NULL, file,
               "non-owner has no initializer");
        for (size_t k = 0; k < jh_json_count(signals); ++k) {
          const jb_json_tree_view_t signal = jh_json_item(signals, k);
          const char *own =
              jh_hw_join(h, JH_PARTS(jh_span(key), jh_span("."),
                                     jh_span(jh_hw_key(h, signal))));
          expect(!text_is(jh_json_field(signal, "owner"), own), file,
                 "no signal owned by non-driving node");
        }
      }
      const jb_json_tree_view_t expected_uart = jh_json_field(test, "uart"),
                                buses = jh_json_field(model, "buses");
      if (expected_uart.tree != NULL) {
        bool found = false;
        for (size_t j = 0; j < jh_json_count(buses); ++j) {
          const jb_json_tree_view_t bus = jh_json_item(buses, j);
          if (!text_is(jh_json_field(bus, "kind"), "uart"))
            continue;
          found = true;
          expect(jh_hw_equal(h, jh_json_field(bus, "baudRate"),
                             jh_json_field(expected_uart, "baudRate")),
                 file, "UART baud");
          expect(text_is(jh_json_field(expected_uart, "frameConfig"),
                         jh_hw_uart_frame(h, jh_json_field(bus, "frame"))),
                 file, "UART frame");
          break;
        }
        expect(found, file, "UART bus exists");
      }
      const jb_json_tree_view_t released =
          jh_json_field(test, "releasedReservations");
      if (released.tree != NULL) {
        jb_json_tree_t *actual = jh_hw_array(h);
        const jb_json_tree_view_t reservations =
            jh_json_field(model, "reservations");
        jb_vector_t keys = {0};
        (void)jh_list_init(&keys);
        jh_hw_keys(h, reservations, &keys);
        for (size_t j = 0; j < jb_vector_size(&keys); ++j) {
          const jb_string_t *key = jh_list_at(&keys, j);
          if (jh_hw_bool(jh_json_field(
                  jh_json_field(reservations, jb_string_data(key)),
                  "released")))
            jh_hw_append(h, actual, jh_hw_string(h, jh_string_span(key)));
        }
        expect(jh_hw_equal(h, jh_hw_root(actual), released), file,
               "released reservations");
        jh_list_destroy(&keys);
      }
      expect(h->memory.status == HAL_OK, file,
             "resolution assertions complete");
    }
    jh_hardware_destroy(&hardware);
    jh_failure_destroy(&failure);
    jb_string_destroy(&assembly);
    jb_string_destroy(&name);
    jb_string_destroy(&path);
  }
}

static hal_status_t stdout_sink(void *user, const char *bytes, size_t length) {
  (void)user;
  return fwrite(bytes, 1, length, stdout) == length ? HAL_OK : HAL_EIO;
}

typedef struct {
  const jh_hardware_registry_t *registry;
  const char *directory;
  jb_string_t tree, header, resolved, program;
} build_test_t;

static hal_status_t write_text(const char *path, jb_json_span_t text) {
  bool changed = false;
  return jb_file_write_atomic(path, text.data, text.length, false, &changed);
}

static bool read_fixture(const jh_hardware_registry_t *registry,
                         const char *fixture, jb_string_t *text) {
  jb_string_t path = {0}, base = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  (void)jb_string_init(&base, jb_heap_allocator());
  hal_status_t status = jh_path_join(jb_string_data(&registry->root),
                                     "tests/fixtures/device_tree", &base);
  if (status == HAL_OK)
    status = jh_path_join(jb_string_data(&base), fixture, &path);
  jb_file_data_t input = {0};
  if (status == HAL_OK)
    status = jb_file_read(jb_string_data(&path), registry->max_bytes, &input);
  if (status == HAL_OK)
    status = jb_string_assign(text, (const char *)input.data, input.length);
  expect(status == HAL_OK, fixture, "read fixture");
  jb_file_release(&input);
  jb_string_destroy(&base);
  jb_string_destroy(&path);
  return status == HAL_OK;
}

static bool save_resolution(build_test_t *test, const char *assembly) {
  jh_hardware_t hardware = {0};
  jh_failure_t failure;
  jh_failure_init(&failure);
  hal_status_t status = resolve(test->registry, jb_string_data(&test->tree),
                                assembly, NULL, &hardware, &failure);
  jb_string_t header = {0}, document = {0};
  (void)jb_string_init(&header, jb_heap_allocator());
  (void)jb_string_init(&document, jb_heap_allocator());
  if (status == HAL_OK)
    status = jh_hardware_header(&hardware, &header, &failure);
  if (status == HAL_OK)
    status = jh_hardware_document(&hardware, &document, &failure);
  if (status == HAL_OK)
    expect(jb_string_size(&document) >= 2 &&
               strcmp(jb_string_data(&document) + jb_string_size(&document) - 2,
                      "}\n") == 0,
           assembly, "resolved document ends with one newline");
  if (status == HAL_OK)
    status = write_text(jb_string_data(&test->header), jh_string_span(&header));
  if (status == HAL_OK)
    status =
        write_text(jb_string_data(&test->resolved), jh_string_span(&document));
  expect(status == HAL_OK, assembly, jb_string_data(&failure.message));
  jb_string_destroy(&document);
  jb_string_destroy(&header);
  jh_failure_destroy(&failure);
  jh_hardware_destroy(&hardware);
  return status == HAL_OK;
}

static bool copy_bindings(build_test_t *test, const char *fixture) {
  jb_string_t base = {0}, source = {0}, destination = {0}, relative = {0};
  jb_string_t *texts[] = {&base, &source, &destination, &relative};
  for (size_t i = 0; i < COUNTOF(texts); ++i)
    (void)jb_string_init(texts[i], jb_heap_allocator());
  jh_failure_t failure;
  jh_failure_init(&failure);
  hal_status_t status = jh_path_join(jb_string_data(&test->registry->root),
                                     "tests/fixtures/device_tree", &base);
  if (status == HAL_OK)
    status = jh_path_join(jb_string_data(&base), fixture, &source);
  if (status == HAL_OK)
    status = jh_path_parent(jb_string_data(&source), &base, &failure);
  jb_json_tree_t *tree = NULL;
  jb_json_tree_view_t root = {0};
  if (status == HAL_OK)
    status =
        jh_check_load_object(jb_string_data(&source), test->registry->max_bytes,
                             &tree, &root, &failure);
  const jb_json_tree_view_t bindings = jh_json_field(root, "bindings");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(bindings); ++i) {
    const char *name = text(jh_json_item(bindings, i), &relative);
    if (name == NULL) {
      status = HAL_ETYPE;
      break;
    }
    status = jh_python_resolve(name, jb_string_data(&base), &source, &failure);
    if (status == HAL_OK)
      status = jh_python_resolve(name, test->directory, &destination, &failure);
    jb_file_data_t bytes = {0};
    if (status == HAL_OK)
      status = jb_file_read(jb_string_data(&source), test->registry->max_bytes,
                            &bytes);
    if (status == HAL_OK)
      status =
          write_text(jb_string_data(&destination),
                     (jb_json_span_t){(const char *)bytes.data, bytes.length});
    jb_file_release(&bytes);
  }
  expect(status == HAL_OK, fixture,
         "copy declared test bindings inside the owned scratch directory");
  jb_json_tree_destroy(tree);
  jh_failure_destroy(&failure);
  for (size_t i = 0; i < COUNTOF(texts); ++i)
    jb_string_destroy(texts[i]);
  return status == HAL_OK;
}

static void finalize_expect(build_test_t *test, const char *variant,
                            const jb_vector_t *definitions, const char *code,
                            const char *detail) {
  jh_hardware_outputs_t outputs;
  jh_hardware_outputs_init(&outputs);
  jh_failure_t failure;
  jh_failure_init(&failure);
  const jh_hardware_build_options_t options = {
      test->registry,
      &test->registry->feature_registry,
      test->directory,
      jb_string_data(&test->resolved),
      jb_string_data(&test->header),
      test->directory,
      variant,
      NULL,
      definitions,
      NULL};
  const hal_status_t status =
      jh_hardware_finalize(&options, &outputs, &failure);
  if (code == NULL) {
    expect(status == HAL_OK, "finalize", jb_string_data(&failure.message));
    expect(strstr(jb_string_data(&outputs.content[JH_HARDWARE_CONFIG]),
                  detail) != NULL,
           "finalize", "configuration identity or program fact");
    expect(strstr(jb_string_data(&outputs.content[JH_HARDWARE_BOARD_HEADER]),
                  "#if JH_HW_HARDWARE_ID != 0x33bb3adda40bac6eULL") != NULL,
           "finalize", "board header belongs to the resolved generation");
    expect(strstr(jb_string_data(&outputs.content[JH_HARDWARE_PACKAGE]),
                  test->directory) == NULL,
           "finalize", "installed record has no build directory");
    expect(strstr(jb_string_data(&outputs.content[JH_HARDWARE_PACKAGE]),
                  "JH_BOARD_INPUT_GLOBS") == NULL,
           "finalize", "installed record has no checkout input globs");
  } else {
    expect(status == HAL_ECONFIG, code, jb_string_data(&failure.message));
    expect(strcmp(jb_string_data(&failure.code), code) == 0, code,
           jb_string_data(&failure.message));
    expect(strstr(jb_string_data(&failure.message), detail) != NULL, detail,
           jb_string_data(&failure.message));
  }
  jh_failure_destroy(&failure);
  jh_hardware_outputs_destroy(&outputs);
}

static void finalize_specifications(build_test_t *t,
                                    jb_json_tree_view_t specification) {
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = calloc(1, sizeof *h);
  expect(h != NULL, "finalize specifications", "test context");
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  jh_hw_init(h, t->registry, &failure);
  const char *late = jh_hw_path(h, t->directory, "jh_board_config.h");
  static const char *const groups[] = {"featureCases", "resolutionCases",
                                       "headerCases"};
  for (size_t group = 0; h->memory.status == HAL_OK && group < COUNTOF(groups);
       ++group) {
    const jb_json_tree_view_t cases =
        jh_json_field(specification, groups[group]);
    for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(cases);
         ++i) {
      const jb_json_tree_view_t test = jh_json_item(cases, i);
      const jb_json_tree_view_t macros = jh_json_field(test, "boardMacros");
      if (group == 1 && jh_json_count(macros) == 0)
        continue;
      const char *file = jh_hw_text(
          h, jh_json_field(test, group == 2 ? "deviceTree" : "file"));
      jb_string_t input = {0};
      (void)jb_string_init(&input, jb_heap_allocator());
      bool prepared =
          read_fixture(t->registry, file, &input) && copy_bindings(t, file) &&
          write_text(jb_string_data(&t->tree), jh_string_span(&input)) ==
              HAL_OK;
      const char *assembly =
          group == 2 ? "pico" : jh_hw_text(h, jh_json_field(test, "assembly"));
      if (prepared)
        prepared = save_resolution(t, assembly);
      if (group == 2) {
        (void)jb_string_resize(&input, 0, '\0');
        prepared =
            prepared &&
            read_fixture(t->registry,
                         jh_hw_text(h, jh_json_field(test, "file")), &input);
      } else {
        prepared = prepared &&
                   jb_string_assign(&input, "#pragma once\n", 13) == HAL_OK;
      }
      prepared = prepared && write_text(jb_string_data(&t->program),
                                        jh_string_span(&input)) == HAL_OK;
      expect(prepared, groups[group], file);
      jb_vector_t definitions = {0};
      (void)jh_list_init(&definitions);
      const jb_json_tree_view_t defs = jh_json_field(test, "definitions");
      for (size_t j = 0; j < jh_json_count(defs); ++j) {
        const jb_json_tree_view_t value = jh_json_item(defs, j);
        jh_node_storage(
            &h->memory,
            jh_list_push_parts(&definitions,
                               JH_PARTS(jh_json_key(value), jh_span("="),
                                        jh_span(jh_hw_value_text(h, value)))));
      }
      jh_hardware_outputs_t outputs;
      jh_hardware_outputs_init(&outputs);
      jh_failure_t result;
      jh_failure_init(&result);
      const jh_hardware_build_options_t options = {
          t->registry,
          &t->registry->feature_registry,
          t->directory,
          jb_string_data(&t->resolved),
          jb_string_data(&t->header),
          t->directory,
          NULL,
          NULL,
          &definitions,
          NULL};
      hal_status_t status =
          prepared ? jh_hardware_finalize(&options, &outputs, &result)
                   : HAL_EIO;
      const char *code = jh_hw_text(h, jh_json_field(test, "error"));
      if (*code) {
        expect(status == HAL_ECONFIG &&
                   strcmp(code, jb_string_data(&result.code)) == 0,
               file, jb_string_data(&result.message));
        expect(jh_hw_is(jh_json_field(test, "at"),
                        jb_string_data(&result.pointer)),
               file, "diagnostic pointer");
        expect(jb_string_size(&outputs.content[JH_HARDWARE_BOARD_HEADER]) == 0,
               file, "failure leaves no late header");
        if (group == 0) {
          expect(strstr(jb_string_data(&result.message),
                        jh_hw_text(h, jh_json_field(test, "definition"))) !=
                     NULL,
                 file, "feature named in diagnostic");
          expect(strstr(jb_string_data(&result.message), "argument --define") !=
                     NULL,
                 file, "definition source");
        } else {
          const char *location = jh_hw_join(
              h, JH_PARTS(
                     jh_span("hal_project_config.h:"),
                     jh_span(jh_hw_value_text(h, jh_json_field(test, "line"))),
                     jh_span(":")));
          expect(strstr(jb_string_data(&result.message), location) != NULL,
                 file, "header line");
        }
      } else {
        expect(status == HAL_OK, file, jb_string_data(&result.message));
        if (status == HAL_OK)
          status = write_text(
              late, jh_string_span(&outputs.content[JH_HARDWARE_BOARD_HEADER]));
        const jb_json_tree_view_t document = jh_hw_root(jh_hw_parse_tree(
            h, jh_string_span(&outputs.content[JH_HARDWARE_CONFIG])));
        const jb_json_tree_view_t enabled = jh_json_field(
            jh_json_field(document, "software"), "resolvedFeatures");
        const jb_json_tree_view_t absent = jh_json_field(test, "doesNotEnable");
        for (size_t j = 0; j < jh_json_count(absent); ++j) {
          const char *feature = jh_hw_text(h, jh_json_item(absent, j));
          expect(!jh_hw_list_has(enabled, feature), file, feature);
        }
      }
      if (jh_json_count(macros) != 0 && prepared) {
        jh_project_config_t config;
        jh_project_config_init(&config);
        jh_failure_destroy(&result);
        jh_failure_init(&result);
        jb_vector_t aliases = {0}, directives = {0};
        (void)jh_list_init(&aliases);
        (void)jh_config_directives_init(&directives);
        status = *code
                     ? HAL_OK
                     : jh_project_config_directives(late, &directives, &result);
        /* The late header includes HAL API headers outside the project's
         * preprocessor subset. Read its requested aliases as directives; the
         * actual C/C++ compile probes exercise all includes and guards. */
        for (size_t j = 0; status == HAL_OK && j < jb_vector_size(&directives);
             ++j) {
          const jh_config_directive_t *directive =
              jb_vector_const_at(&directives, j);
          if (!directive->undef &&
              jh_json_field(macros, jb_string_data(&directive->name)).tree !=
                  NULL)
            status = jh_list_push_parts(
                &aliases,
                JH_PARTS(jh_string_span(&directive->name), jh_span("="),
                         jh_string_span(&directive->text)));
        }
        if (status == HAL_OK)
          status = jh_project_config_read(
              "", &aliases, jb_string_data(&t->header), NULL, &config, &result);
        expect(status == HAL_OK, file, jb_string_data(&result.message));
        for (size_t j = 0; status == HAL_OK && j < jh_json_count(macros); ++j) {
          const jb_json_tree_view_t expected = jh_json_item(macros, j);
          const char *name = jh_hw_key(h, expected);
          if (*code && strncmp(name, "HAL_BOARD_HAS_", 14) == 0)
            name = jh_hw_join(
                h, JH_PARTS(jh_span("JH_HW_HAS_"), jh_span(name + 14)));
          int64_t number = 0;
          jh_config_value_t value;
          jh_config_value_init(&value);
          expect(jh_json_int64(expected, &number) &&
                     jh_project_config_value(&config, jh_span(name), &value,
                                             &result) == HAL_OK &&
                     value.kind == JH_CONFIG_INTEGER_VALUE &&
                     value.integer == number,
                 file, name);
          jh_config_value_destroy(&value);
        }
        jh_project_config_destroy(&config);
        jh_config_directives_destroy(&directives);
        jh_list_destroy(&aliases);
      }
      jh_failure_destroy(&result);
      jh_hardware_outputs_destroy(&outputs);
      jh_list_destroy(&definitions);
      jb_string_destroy(&input);
    }
  }
  expect(h->memory.status == HAL_OK, "finalize specifications",
         jb_string_data(&failure.message));
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void finalize_cases(const jh_hardware_registry_t *registry,
                           const char *work,
                           jb_json_tree_view_t specification) {
  const jb_json_tree_view_t cases = jh_json_field(specification, "cases");
  jb_temp_dir_t *scratch = NULL;
  if (jb_temp_dir_create(work, &scratch) != HAL_OK) {
    expect(false, "finalize", "scratch directory");
    return;
  }
  build_test_t t = {0};
  t.registry = registry;
  jb_string_t project = {0};
  (void)jb_string_init(&project, jb_heap_allocator());
  expect(jh_path_join(jb_temp_dir_path(scratch), "a/b", &project) == HAL_OK,
         "finalize", "project directory");
  t.directory = jb_string_data(&project);
  jb_string_t *paths[] = {&t.tree, &t.header, &t.resolved, &t.program};
  const char *const names[] = {"device_tree.json", "jh_hardware.h",
                               "jh_hardware_resolved.json",
                               "hal_project_config.h"};
  for (size_t i = 0; i < COUNTOF(paths); ++i) {
    (void)jb_string_init(paths[i], jb_heap_allocator());
    expect(jh_path_join(t.directory, names[i], paths[i]) == HAL_OK, "finalize",
           "test path");
  }
  jb_string_t input = {0};
  (void)jb_string_init(&input, jb_heap_allocator());
  jh_failure_t failure;
  jh_failure_init(&failure);
  hal_status_t status = HAL_OK;
  jb_vector_t definitions = {0};
  (void)jh_list_init(&definitions);
  if (status == HAL_OK &&
      read_fixture(registry, "valid/status/device_tree.json", &input) &&
      copy_bindings(&t, "valid/status/device_tree.json") &&
      write_text(jb_string_data(&t.tree), jh_string_span(&input)) == HAL_OK &&
      save_resolution(&t, "pico")) {
    finalize_expect(
        &t, NULL, &definitions, NULL,
        "f78a4a9142963ff5ae565422c327c03d1cf89185cda1adb624e72c7216ca48b6");
    finalize_expect(&t, "MISSING", &definitions, "JH-HW-FEATURE", "MISSING");
    (void)jh_list_push(&definitions, jh_span("HAL_LED_BUILTIN=3"));
    finalize_expect(&t, NULL, &definitions, "JH-HW-FEATURE",
                    "argument --define");
    jh_list_destroy(&definitions);
    (void)jh_list_init(&definitions);
    expect(write_text(jb_string_data(&t.program),
                      jh_span("#pragma once\n#if 0\n#define HAL_LED_BUILTIN "
                              "15u\n#endif\n")) == HAL_OK,
           "ownership", "write inactive definition");
    finalize_expect(&t, NULL, &definitions, "JH-HW-FEATURE",
                    "hal_project_config.h:3:");
    expect(
        write_text(jb_string_data(&t.program),
                   jh_span("#pragma once\n#undef JH_HW_NODE_LED_PIN_OUT\n")) ==
            HAL_OK,
        "ownership", "write undefinition");
    finalize_expect(&t, NULL, &definitions, "JH-HW-FEATURE",
                    "hal_project_config.h:2:");
    expect(write_text(jb_string_data(&t.program),
                      jh_span("#if 0\n#define JH_PROJECT_TARGETS(X) "
                              "X(HAL_TARGET_MOCK)\n#endif\n")) == HAL_OK,
           "targets", "write inactive target declaration");
    finalize_expect(&t, NULL, &definitions, "JH-HW-TARGETS",
                    "hal_project_config.h:2:");
    expect(
        write_text(
            jb_string_data(&t.program),
            jh_span(
                "#if JH_HW_NODE_LED_PIN_OUT == 14U\n#define HAL_TEST_TUNABLE "
                "42\n#else\n#error wrong early header\n#endif\n")) == HAL_OK,
        "preinclude", "write program using hardware");
    finalize_expect(&t, NULL, &definitions, NULL,
                    "\"HAL_TEST_TUNABLE\": \"42\"");
    expect(write_text(jb_string_data(&t.header),
                      jh_span("#define WRONG_HEADER 1\n")) == HAL_OK,
           "stale", "replace header");
    finalize_expect(&t, NULL, &definitions, "JH-HW-GENERATION",
                    "not the one this model produces");
    if (save_resolution(&t, "pico")) {
      expect(jh_append(&input, "\n") == HAL_OK &&
                 write_text(jb_string_data(&t.tree), jh_string_span(&input)) ==
                     HAL_OK,
             "stale", "change input bytes without changing hardware");
      finalize_expect(&t, NULL, &definitions, "JH-HW-GENERATION",
                      "device_tree.json changed after resolve");
    }
  }
  expect(write_text(jb_string_data(&t.program), jh_span("#pragma once\n")) ==
             HAL_OK,
         "finalize", "clear program");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(cases); ++i) {
    const jb_json_tree_view_t test = jh_json_item(cases, i);
    if (!text_is(jh_json_field(test, "phase"), "finalize"))
      continue;
    jb_string_t file = {0}, assembly = {0}, code = {0}, at = {0};
    jb_string_t *texts[] = {&file, &assembly, &code, &at};
    for (size_t j = 0; j < COUNTOF(texts); ++j)
      (void)jb_string_init(texts[j], jb_heap_allocator());
    const char *name = text(jh_json_field(test, "file"), &file);
    if (name != NULL && read_fixture(registry, name, &input) &&
        copy_bindings(&t, name) &&
        write_text(jb_string_data(&t.tree), jh_string_span(&input)) == HAL_OK &&
        save_resolution(
            &t, text(jh_json_field(jh_json_field(test, "input"), "assembly"),
                     &assembly))) {
      const jb_json_tree_view_t defs = jh_json_field(test, "definitions");
      jh_list_destroy(&definitions);
      (void)jh_list_init(&definitions);
      for (size_t j = 0; j < jh_json_count(defs); ++j) {
        const jb_json_tree_view_t def = jh_json_item(defs, j);
        jb_json_span_t value = {0};
        (void)jb_json_tree_text(def, &value);
        expect(jh_list_push_parts(
                   &definitions,
                   JH_PARTS(jh_json_key(def), jh_span("="), value)) == HAL_OK,
               "providers", "definition");
      }
      finalize_expect(&t, NULL, &definitions,
                      text(jh_json_field(test, "error"), &code),
                      text(jh_json_field(test, "at"), &at));
    }
    for (size_t j = 0; j < COUNTOF(texts); ++j)
      jb_string_destroy(texts[j]);
  }
  jh_list_destroy(&definitions);
  finalize_specifications(&t, specification);
  jh_failure_destroy(&failure);
  jb_string_destroy(&input);
  for (size_t i = 0; i < COUNTOF(paths); ++i)
    jb_string_destroy(paths[i]);
  jb_string_destroy(&project);
  expect(jb_temp_dir_remove(&scratch) == HAL_OK, "finalize", "scratch cleanup");
}

static void i2c_cases(const jh_hardware_registry_t *registry, const char *work,
                      jb_json_tree_view_t cases) {
  static const char prefix[] =
      "{\"schemaVersion\":1,\"nodes\":{\"io0\":{\"compatible\":\"jaszczurhal,"
      "i2c-device\"},"
      "\"io1\":{\"compatible\":\"jaszczurhal,i2c-device\"}},\"assemblies\":{"
      "\"pico\":{"
      "\"target\":\"rp2040\",\"board\":\"pico\",\"clock\":{\"tree\":"
      "\"default\"},"
      "\"statusLed\":{\"node\":\"board.statusLed\"},\"buses\":{\"i2c\":{"
      "\"kind\":\"i2c\","
      "\"index\":0,\"frequencyHz\":100000,\"pins\":{\"sda\":0,\"scl\":1}}},"
      "\"nodes\":{\"io0\":{\"present\":true,\"bus\":\"i2c\",\"address\":";
  static const char middle[] =
      "},\"io1\":{\"present\":true,\"bus\":\"i2c\",\"address\":";
  jb_string_t directory = {0}, path = {0}, input = {0}, equivalent = {0},
              literal = {0};
  (void)jb_string_init(&directory, jb_heap_allocator());
  (void)jb_string_init(&path, jb_heap_allocator());
  (void)jb_string_init(&input, jb_heap_allocator());
  (void)jb_string_init(&equivalent, jb_heap_allocator());
  (void)jb_string_init(&literal, jb_heap_allocator());
  hal_status_t status = jh_path_join(work, "hardware-i2c", &directory);
  if (status == HAL_OK)
    status =
        jh_path_join(jb_string_data(&directory), "device_tree.json", &path);
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(cases); ++i) {
    const jb_json_tree_view_t test = jh_json_item(cases, i);
    const char *token = text(jh_json_field(test, "literal"), &literal);
    int64_t normalized = -1;
    expect(jh_json_int64(jh_json_field(test, "normalized"), &normalized), "I2C",
           "normalized fixture address");
    const bool collision = jh_hw_bool(jh_json_field(test, "collision"));
    status = jh_join(
        &input, JH_PARTS(jh_span(prefix), jh_span(token), jh_span(middle),
                         jh_span(collision ? "81" : "82"), jh_span("}}}}}")));
    bool changed = false;
    if (status == HAL_OK)
      status =
          jb_file_write_atomic(jb_string_data(&path), jb_string_data(&input),
                               jb_string_size(&input), false, &changed);
    if (status != HAL_OK)
      break;
    jh_hardware_t hardware = {0};
    jh_failure_t failure;
    jh_failure_init(&failure);
    const hal_status_t resolved = resolve(registry, jb_string_data(&path),
                                          "pico", NULL, &hardware, &failure);
    if (normalized < 0 || collision) {
      expect(resolved == HAL_ECONFIG, token,
             "invalid address or duplicate rejected");
      expect(strcmp(jb_string_data(&failure.code),
                    collision ? "JH-HW-ADDRESS" : "JH-HW-SCHEMA") == 0,
             token, jb_string_data(&failure.message));
      expect(strcmp(jb_string_data(&failure.pointer),
                    collision ? "/assemblies/pico/nodes/io1/address"
                              : "/assemblies/pico/nodes/io0/address") == 0,
             token, jb_string_data(&failure.message));
      if (collision)
        expect(strstr(jb_string_data(&failure.message),
                      "address 0x51 on i2c is already used") != NULL,
               token, "collision uses the normalized hexadecimal address");
    } else {
      expect(resolved == HAL_OK, token, jb_string_data(&failure.message));
      const jb_json_tree_view_t model = jh_hardware_model(&hardware);
      const jb_json_tree_view_t node = jh_json_field(
          jh_json_field(jh_json_field(model, "hardware"), "nodes"), "io0");
      int64_t address = -1;
      expect(jh_json_int64(jh_json_field(node, "address"), &address) &&
                 address == normalized,
             token, "model keeps an integer address");
      jb_string_t header = {0};
      (void)jb_string_init(&header, jb_heap_allocator());
      expect(jh_hardware_header(&hardware, &header, &failure) == HAL_OK, token,
             "render address");
      char macro[80];
      (void)snprintf(macro, sizeof macro,
                     "#define JH_HW_NODE_IO0_ADDRESS 0x%02xU",
                     (unsigned)normalized);
      expect(strstr(jb_string_data(&header), macro) != NULL, token,
             "header uses hexadecimal");
      if (normalized == 81) {
        if (jb_string_size(&equivalent) == 0)
          status = jh_append_span(&equivalent, jh_string_span(&header));
        else
          expect(jh_span_equal(jh_string_span(&header),
                               jh_string_span(&equivalent)),
                 token,
                 "equivalent address spellings retain the digest and complete "
                 "header");
      }
      jb_string_destroy(&header);
    }
    jh_failure_destroy(&failure);
    jh_hardware_destroy(&hardware);
  }
  expect(status == HAL_OK, "i2c fixtures", "write test input");
  if (jb_string_size(&directory) != 0)
    expect(jb_fs_remove(jb_string_data(&directory), true) == HAL_OK,
           "i2c fixtures", "remove scratch directory");
  jb_string_destroy(&equivalent);
  jb_string_destroy(&literal);
  jb_string_destroy(&input);
  jb_string_destroy(&path);
  jb_string_destroy(&directory);
}

static void effective_cases(const jh_hardware_registry_t *registry,
                            const char *work) {
  static const struct {
    const char *name, *manifest, *settings, *header, *expected, *finding;
  } cases[] = {
      {"axis-free", "{}", "{}", "#define HAL_ENABLE_CRC\n",
       "{\"target\":null,\"board\":null,\"requestedFeatures\":[\"HAL_ENABLE_"
       "CRC\"]}",
       NULL},
      {"board chooses default target", "{\"board\":\"pico\"}", "{}",
       "#if defined(HAL_TARGET_RP2040)\n#define HAL_ENABLE_KV\n#else\n#define "
       "HAL_ENABLE_UDP\n#endif\n",
       "{\"target\":\"rp2040\",\"board\":\"pico\",\"requestedFeatures\":[\"HAL_"
       "ENABLE_KV\"],"
       "\"resolvedFeatures\":[\"HAL_ENABLE_CRC\",\"HAL_ENABLE_EEPROM\",\"HAL_"
       "ENABLE_KV\"]}",
       NULL},
      {"empty profiles", "{\"targetProfiles\":{}}", "{}", "",
       "{\"target\":\"rp2040\",\"board\":\"pico\"}", NULL},
      {"target path token", "{\"buildDir\":\".build/${target}\"}", "{}", "",
       "{\"target\":\"rp2040\",\"board\":\"pico\"}", NULL},
      {"settings target path token", "{}",
       "{\"jaszczurhal.buildDir\":\".build/${target}\"}", "",
       "{\"target\":\"rp2040\",\"board\":\"pico\"}", NULL},
      {"manifest build directory wins", "{\"buildDir\":\".build\"}",
       "{\"jaszczurhal.buildDir\":\".build/${target}\"}", "",
       "{\"target\":null,\"board\":null}", NULL},
      {"unknown field cannot select a target",
       "{\"unrecognized\":\"${target}\"}", "{}", "",
       "{\"target\":null,\"board\":null}", NULL},
      {"default target rejects base build", "{\"board\":\"pico\"}", "{}",
       "#if defined(HAL_TARGET_RP2040)\n#error no RP build\n#endif\n", NULL,
       "no RP build"},
      {"invalid USB number", "{\"identity\":{\"usbVid\":65536}}", "{}", "",
       NULL,
       "invalid USB identity configuration: usbVid must be in range 0..65535"},
      {"huge USB integer", "{\"identity\":{\"usbVid\":92233720368547758080}}",
       "{}", "", NULL, "usbVid must be in range 0..65535"},
      {"invalid USB token", "{\"identity\":{\"usbPid\":\"0x12g\"}}", "{}", "",
       NULL, "usbPid must be an integer or a USB hexadecimal identifier"},
      {"negative USB", "{\"identity\":{\"usbVid\":\"-1\"}}", "{}", "", NULL,
       "usbVid must be in range 0..65535"},
      {"unsigned USB forms",
       "{\"identity\":{\"usbVid\":\" +00_1 \" ,\"usbPid\":\"0x_FF\"}}", "{}",
       "", "{\"target\":null,\"board\":null}", NULL},
      {"Unicode decimal USB",
       "{\"identity\":{\"usbVid\":\"\\u0661\\u0662\\u0663\\u0664\","
       "\"usbPid\":\"\\ud835\\udfd8\\ud835\\udfd9\"}}",
       "{}", "", "{\"target\":null,\"board\":null}", NULL},
      {"USB floats and bools",
       "{\"identity\":{\"usbVid\":1e99,\"usbPid\":true}}", "{}", "",
       "{\"target\":null,\"board\":null}", NULL},
      {"USB syntax wins over range",
       "{\"identity\":{\"usbPid\":\"999999999z\"}}", "{}", "", NULL,
       "usbPid must be an integer or a USB hexadecimal identifier"},
      {"invalid settings", "{}", "[]", "", NULL, "must contain a JSON object"},
      {"settings syntax location", "{}", "{\"x\":1,}", "", NULL,
       "settings.json:1:8"},
      {"invalid identity type", "{\"identity\":\"bad\"}", "{}", "", NULL,
       "field 'identity' must be an object"},
      {"falsey identity", "{\"identity\":false}", "{}", "", "{}", NULL},
      {"identity character pairs", "{\"identity\":[\"ab\",\"\\u0105b\"]}", "{}",
       "", "{}", NULL},
      {"managed target replaces cache",
       "{\"target\":\"rp2040\",\"cmake\":{\"cache\":[1]}}", "{}",
       "#define JH_PROJECT_VARIANTS(X) X(TLS, \"tls\", HAL_ENABLE_TLS = 1)\n",
       "{\"target\":\"rp2040\"}", NULL},
      {"identity pairs", "{\"identity\":[[\"usbVid\",65536]]}", "{}", "", NULL,
       "usbVid must be in range 0..65535"},
      {"identity pair key with NUL",
       "{\"identity\":[[\"usbVid\\u0000x\",65536]]}", "{}", "", "{}", NULL},
      {"missing device tree for assembly", "{\"assembly\":\"desk\"}", "{}", "",
       NULL, "[JH-HW-ASSEMBLY]"},
      {"invalid variant cmake", "{\"cmake\":\"bad\"}", "{}",
       "#define JH_PROJECT_VARIANTS(X) X(TLS, \"tls\", HAL_ENABLE_TLS = 1)\n",
       "{}", "field 'cmake' must be an object"},
      {"invalid variant cache", "{\"cmake\":{\"cache\":\"bad\"}}", "{}",
       "#define JH_PROJECT_VARIANTS(X) X(TLS, \"tls\", HAL_ENABLE_TLS = 1)\n",
       "{}", "field 'cmake.cache' must be an object"},
      {"foreign root", "{}", "{\"jaszczurhal.root\":\"other\"}", "", NULL,
       "[JH-HAL-ROOT] .vscode/settings.json: jaszczurhal.root is "},
      {"foreign dispatcher",
       "{\"cmake\":{\"sourceDir\":\"other/cmake/jh_firmware_project\"}}", "{}",
       "", NULL,
       "[JH-HAL-ROOT] .vscode/jaszczurhal.project.json: cmake.sourceDir is the "
       "dispatcher of "},
      {"profile dispatcher",
       "{\"targetProfiles\":{\"rp2040\":{\"cmake\":{\"sourceDir\":\"other/"
       "cmake/jh_firmware_project\"}}}}",
       "{}", "", NULL,
       "[JH-HAL-ROOT] registry/targetProfiles:rp2040: cmake.sourceDir is the "
       "dispatcher of "},
      {"profile fixes dispatcher",
       "{\"target\":\"rp2040\",\"cmake\":{\"sourceDir\":\"other/cmake/"
       "jh_firmware_project\"},"
       "\"targetProfiles\":{\"rp2040\":{\"cmake\":{\"sourceDir\":\"${jhRoot}/"
       "cmake/jh_firmware_project\"}}}}",
       "{}", "", "{\"target\":\"rp2040\",\"board\":\"pico\"}", NULL},
      {"integer target", "{\"target\":123}", "{}", "", NULL,
       "[JH-CFG-TARGET] unknown target '123'"},
      {"integer board", "{\"board\":123}", "{}", "", NULL,
       "board '123' is not in the board registry"},
  };
  jh_failure_t memory_failure;
  jh_failure_init(&memory_failure);
  jh_hw_t *h = calloc(1, sizeof *h);
  expect(h != NULL, "effective lint", "test context allocation");
  if (h == NULL) {
    jh_failure_destroy(&memory_failure);
    return;
  }
  jh_hw_init(h, registry, &memory_failure);
  const char *directory = jh_hw_path(h, work, "effective-\xc3\xa9");
  const char *manifest =
      jh_hw_path(h, directory, ".vscode/jaszczurhal.project.json");
  const char *settings = jh_hw_path(h, directory, ".vscode/settings.json");
  const char *header = jh_hw_path(h, directory, "hal_project_config.h");
  jb_vector_t roots = {0}, inputs = {0};
  (void)jh_list_init(&roots);
  (void)jh_list_init(&inputs);
  jh_node_storage(&h->memory, jh_list_push(&roots, jh_span(work)));
  jh_node_storage(&h->memory, jh_list_push(&inputs, jh_span(manifest)));
  jh_node_storage(&h->memory, jh_list_push(&inputs, jh_span(header)));
  jh_list_sort(&inputs);
  for (size_t i = 0; h->memory.status == HAL_OK && i < COUNTOF(cases); ++i) {
    hal_status_t status = write_text(manifest, jh_span(cases[i].manifest));
    if (status == HAL_OK)
      status = write_text(settings, jh_span(cases[i].settings));
    if (status == HAL_OK)
      status = write_text(header, jh_span(cases[i].header));
    jb_vector_t findings = {0};
    (void)jh_list_init(&findings);
    jb_string_t output = {0};
    (void)jb_string_init(&output, jb_heap_allocator());
    jh_failure_t failure;
    jh_failure_init(&failure);
    size_t count = SIZE_MAX;
    if (status == HAL_OK)
      status =
          jh_effective_features(registry, &registry->feature_registry, &roots,
                                &inputs, &output, &count, &findings, &failure);
    expect(status == HAL_OK, cases[i].name, jb_string_data(&failure.message));
    const size_t expected_count =
        cases[i].expected == NULL ? 0u
        : cases[i].finding == NULL &&
                strstr(cases[i].header, "JH_PROJECT_VARIANTS") != NULL
            ? 2u
            : 1u;
    expect(count == expected_count, cases[i].name, "configuration count");
    if (cases[i].finding != NULL) {
      expect(jb_vector_size(&findings) == 1 &&
                 strstr(jb_string_data(jh_list_at(&findings, 0)),
                        cases[i].finding) != NULL,
             cases[i].name,
             jb_vector_size(&findings)
                 ? jb_string_data(jh_list_at(&findings, 0))
                 : "missing finding");
      if (i + 1 == COUNTOF(cases)) {
        /* Findings are appended to a caller-owned list; repeating one batch
         * must not add the same diagnostic again. */
        expect(jh_effective_features(registry, &registry->feature_registry,
                                     &roots, &inputs, &output, &count,
                                     &findings, &failure) == HAL_OK &&
                   jb_vector_size(&findings) == 1,
               "effective findings", "deduplicate repeated diagnostics");
      }
    } else if (status == HAL_OK) {
      expect(jb_vector_size(&findings) == 0, cases[i].name,
             jb_vector_size(&findings)
                 ? jb_string_data(jh_list_at(&findings, 0))
                 : "unexpected finding");
      const jb_json_tree_view_t result =
          jh_hw_root(jh_hw_parse_tree(h, jh_string_span(&output)));
      const jb_json_tree_view_t row =
          jh_json_item(jh_json_field(result, "configurations"), 0);
      const jb_json_tree_view_t expected =
          jh_hw_root(jh_hw_parse_tree(h, jh_span(cases[i].expected)));
      for (size_t j = 0; j < jh_json_count(expected); ++j) {
        const jb_json_tree_view_t field = jh_json_item(expected, j);
        const char *key = jh_hw_key(h, field);
        expect(jh_hw_equal(h, field, jh_json_field(row, key)), cases[i].name,
               key);
      }
      expect(strstr(jb_string_data(&output), "effective-\\u00e9") != NULL,
             cases[i].name, "ASCII JSON report preserves Unicode project name");
    }
    jh_failure_destroy(&failure);
    jb_string_destroy(&output);
    jh_list_destroy(&findings);
  }
  expect(h->memory.status == HAL_OK, "effective lint",
         jb_string_data(&memory_failure.message));
  jh_list_destroy(&inputs);
  jh_list_destroy(&roots);
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&memory_failure);
}

static void assembly_effective_cases(const jh_hardware_registry_t *registry,
                                     const char *work) {
  static const struct {
    const char *manifest, *expected;
  } cases[] = {
      {"{\"target\":\"rp2040\"}", "'target': device_tree.json selects"},
      {"{\"board\":\"pico\"}", "'board': device_tree.json selects"},
      {"{\"targetProfiles\":{\"rp2040\":{\"board\":\"pico\"}}}",
       "targetProfiles.rp2040.board: device_tree.json selects"},
      {"{\"cmake\":{\"cache\":{\"JH_BOARD\":\"pico\"}}}",
       "cmake.cache.JH_BOARD: device_tree.json selects"},
      {"{\"targetProfiles\":{\"rp2040\":{\"cmake\":{\"cache\":{\"JH_ASSEMBLY\":"
       "\"pico\"}}}}}",
       "targetProfiles.rp2040.cmake.cache.JH_ASSEMBLY: device_tree.json "
       "selects"},
      {"{\"buildDir\":5}", "field 'buildDir' must be a string"},
      {"{\"targetProfiles\":{\"rp2040\":{\"buildDir\":[\"bad\"]}}}",
       "field 'buildDir' must be a string"},
      {"{\"target\":\"rp2040\",\"identity\":{\"usbVid\":\"zz\"}}",
       "invalid USB identity configuration"},
      {"{\"buildDir\":0}", NULL},
      {"{\"buildDir\":5,\"targetProfiles\":{\"rp2040\":{\"buildDir\":\"."
       "build\"}}}",
       NULL},
  };
  jh_failure_t failure;
  jh_failure_init(&failure);
  jb_string_t directory = {0}, manifest = {0}, header = {0}, tree = {0},
              out = {0};
  jb_string_t *strings[] = {&directory, &manifest, &header, &tree, &out};
  for (size_t i = 0; i < COUNTOF(strings); ++i)
    (void)jb_string_init(strings[i], jb_heap_allocator());
  hal_status_t status = jh_path_join(work, "assembly-effective", &directory);
  if (status == HAL_OK)
    status = jh_path_join(jb_string_data(&directory),
                          ".vscode/jaszczurhal.project.json", &manifest);
  if (status == HAL_OK)
    status = jh_path_join(jb_string_data(&directory), "hal_project_config.h",
                          &header);
  if (status == HAL_OK)
    status =
        jh_path_join(jb_string_data(&directory), "device_tree.json", &tree);
  if (status == HAL_OK)
    status = write_text(jb_string_data(&header), jh_span(""));
  if (status == HAL_OK)
    status = write_text(
        jb_string_data(&tree),
        jh_span(
            "{\"schemaVersion\":1,\"nodes\":{},\"assemblies\":{\"pico\":{"
            "\"target\":\"rp2040\",\"board\":\"pico\",\"clock\":{\"tree\":"
            "\"default\"},"
            "\"statusLed\":{\"node\":\"board.statusLed\"},\"nodes\":{}}}}"));
  jb_vector_t roots = {0}, inputs = {0};
  (void)jh_list_init(&roots);
  (void)jh_list_init(&inputs);
  if (status == HAL_OK)
    status = jh_list_push(&roots, jh_span(work));
  if (status == HAL_OK)
    status = jh_list_push(&inputs, jh_string_span(&manifest));
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(cases); ++i) {
    status = write_text(jb_string_data(&manifest), jh_span(cases[i].manifest));
    jb_vector_t findings = {0};
    (void)jh_list_init(&findings);
    size_t count = SIZE_MAX;
    if (status == HAL_OK)
      status =
          jh_effective_features(registry, &registry->feature_registry, &roots,
                                &inputs, &out, &count, &findings, &failure);
    expect(status == HAL_OK, cases[i].manifest,
           jb_string_data(&failure.message));
    expect(count == (cases[i].expected ? 0u : 1u), cases[i].manifest,
           "configuration count");
    expect(cases[i].expected
               ? jb_vector_size(&findings) == 1 &&
                     strstr(jb_string_data(jh_list_at(&findings, 0)),
                            cases[i].expected) != NULL
               : jb_vector_size(&findings) == 0,
           cases[i].manifest,
           jb_vector_size(&findings) ? jb_string_data(jh_list_at(&findings, 0))
                                     : "missing finding");
    jh_list_destroy(&findings);
  }
  jh_list_destroy(&inputs);
  jh_list_destroy(&roots);
  expect(jb_fs_remove(jb_string_data(&directory), true) == HAL_OK,
         "assembly effective", "cleanup");
  for (size_t i = 0; i < COUNTOF(strings); ++i)
    jb_string_destroy(strings[i]);
  jh_failure_destroy(&failure);
}

static jh_hw_t *test_context(const jh_hardware_registry_t *registry,
                             jh_failure_t *failure) {
  jh_hw_t *h = calloc(1, sizeof *h);
  if (h != NULL) {
    jh_hw_init(h, registry, failure);
    h->path = "literal.json";
  }
  expect(h != NULL, "hardware", "allocate test context");
  return h;
}

static void renderer_cases(const jh_hardware_registry_t *registry,
                           jb_json_tree_view_t cases) {
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  static const char *const pointers[] = {
      "/transport/frequencyHz", "/transport/mode", "/transport/bitOrder"};
  for (size_t i = 0; i < jh_json_count(cases); ++i) {
    const jb_json_tree_view_t test = jh_json_item(cases, i),
                              frame = jh_json_field(test, "frame");
    const char *name = jh_hw_text(h, jh_json_field(test, "renderer"));
    if (frame.tree != NULL) {
      expect(strncmp(name, "uart-", 5) == 0 &&
                 jh_hw_uart_problem(h, name + 5, frame) != NULL,
             name, "UART frame rejected");
      expect(text_is(jh_json_field(test, "at"), "/frame/dataBits"), name,
             "UART rejection field");
      continue;
    }
    const jb_json_tree_view_t transport = jh_json_field(test, "transport");
    int64_t rate = 0, mode = 0;
    (void)jh_hw_i64(h, jh_json_field(transport, "frequencyHz"), &rate);
    (void)jh_hw_i64(h, jh_json_field(transport, "mode"), &mode);
    size_t field = COUNTOF(pointers);
    const char *problem = jh_hw_transport_problem(
        h, name, rate, mode, jh_json_field(transport, "bitOrder"), &field);
    if (jh_json_field(test, "error").tree != NULL) {
      expect(problem != NULL && field < COUNTOF(pointers) &&
                 text_is(jh_json_field(test, "at"), pointers[field]),
             name, "first unsupported transport setting");
    } else {
      const jh_hw_renderer_t *info = jh_hw_renderer_info(name);
      expect(problem == NULL && info != NULL, name, "supported transport");
      if (info != NULL) {
        expect(text_is(jh_json_field(test, "configType"), info->config_type),
               name, "driver config type");
        const jb_json_tree_view_t clock = jh_json_field(test, "clockField");
        expect(info->clock_field == NULL ? clock.tree == NULL
                                         : text_is(clock, info->clock_field),
               name, "SPI clock field");
      }
    }
  }
  expect(h->memory.status == HAL_OK, "renderer cases",
         jb_string_data(&failure.message));
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void board_source_case(jh_hardware_registry_t *registry) {
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  /* The old Python test supplied a registry copy without HSE. Keep the
   * shared registry intact after this single resolution. */
  for (size_t i = 0; i < jb_vector_size(&registry->boards.boards); ++i) {
    jh_board_descriptor_t *board = jb_vector_at(&registry->boards.boards, i);
    if (strcmp(jb_string_data(&board->id), "nucleo-g474re") != 0)
      continue;
    const jb_json_tree_view_t saved = board->root;
    jb_json_tree_t *edited = jh_hw_copy(h, saved);
    jb_json_tree_t *sources =
        jh_hw_copy(h, jh_json_field(saved, "clockSources"));
    jb_json_tree_t *gpio = jh_hw_copy(h, jh_json_field(saved, "gpio"));
    jb_json_tree_t *reservations =
        jh_hw_copy(h, jh_json_field(jh_hw_root(gpio), "reservations"));
    jh_node_storage(&h->memory, jb_json_tree_erase(sources, jh_span("hse")));
    jh_node_storage(&h->memory,
                    jb_json_tree_erase(reservations, jh_span("hse-crystal")));
    jh_hw_set(h, gpio, "reservations", jh_hw_root(reservations));
    jh_hw_set(h, edited, "clockSources", jh_hw_root(sources));
    jh_hw_set(h, edited, "gpio", jh_hw_root(gpio));
    expect(h->memory.status == HAL_OK, "board clock source",
           "prepare board without HSE");
    board->root = jh_hw_root(edited);
    jh_hardware_t hardware = {0};
    const char *file = jh_hw_path(
        h, jb_string_data(&registry->root),
        "tests/fixtures/device_tree/hal/nucleo-canhat/device_tree.json");
    const hal_status_t status =
        resolve(registry, file, "canhat", NULL, &hardware, &failure);
    expect(status == HAL_ECONFIG &&
               strcmp(jb_string_data(&failure.code), "JH-HW-CLOCK") == 0 &&
               strcmp(jb_string_data(&failure.pointer),
                      "/assemblies/canhat/clock/tree") == 0,
           "board clock source", jb_string_data(&failure.message));
    jh_hardware_destroy(&hardware);
    board->root = saved;
    break;
  }
#ifdef _WIN32
  h->base = "C:/project";
  expect(strcmp(jh_hw_source_key(h, "D:/HAL/boards/profile.json"),
                "D:/HAL/boards/profile.json") == 0,
         "source location", "another drive keeps an absolute path");
  expect(strcmp(jh_hw_source_key(h, "c:/PROJECT/binding.json"),
                "binding.json") == 0,
         "source location",
         "drive and directory case do not change relative paths");
  h->base = "//server/share/project";
  expect(strcmp(jh_hw_source_key(h, "//server/other/binding.json"),
                "//server/other/binding.json") == 0,
         "source location", "another UNC share keeps an absolute path");
#else
  h->base = "/project/a\\b";
  expect(strcmp(jh_hw_source_key(h, "/project/binding.json"),
                "../binding.json") == 0,
         "source location", "POSIX backslash belongs to the directory name");
#endif
  expect(strcmp(jh_hw_source_key(h, h->base), ".") == 0, "source location",
         "same directory");
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void catalogue_cases(const jh_hardware_registry_t *registry,
                            const char *work) {
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  static const char *const names[] = {"pimoroni,pim730",
                                      "waveshare,core1262-hf",
                                      "embeddedgarage,can-fd-hat-g474",
                                      "microchip,mcp2515",
                                      "ilitek,ili9341",
                                      "semtech,sx1262",
                                      "infineon,cyw43439"};
  for (size_t i = 0; i < COUNTOF(names); ++i)
    expect(jh_catalogue_find(&registry->catalogue, jh_span(names[i])) != NULL,
           names[i], "HAL type loaded and checked");
  const jh_binding_t *core =
      jh_catalogue_find(&registry->catalogue, jh_span(names[1]));
  const jh_binding_t *pim =
      jh_catalogue_find(&registry->catalogue, jh_span(names[0]));
  if (core != NULL && pim != NULL) {
    const jb_json_tree_view_t core_data =
        jh_hw_import(h, jh_binding_root(&h->memory, core));
    const jb_json_tree_view_t pim_data =
        jh_hw_import(h, jh_binding_root(&h->memory, pim));
    const jb_json_tree_view_t connections =
        jh_json_field(core_data, "connections");
    expect(text_is(jh_json_field(jh_json_field(connections, "rxen"), "owner"),
                   "radio.rfSwitchA"),
           names[1], "RX owner");
    expect(text_is(jh_json_field(jh_json_field(connections, "txen"), "owner"),
                   "radio.rfSwitchB"),
           names[1], "TX owner");
    const jb_json_tree_view_t aliases = jh_json_field(
        jh_json_field(jh_json_field(pim_data, "connections"), "wlOn"),
        "aliases");
    expect(jh_json_count(aliases) == 1 &&
               text_is(jh_json_item(aliases, 0), "radio.btOn"),
           names[0], "radio alias");
  }
  jb_temp_dir_t *scratch = NULL;
  hal_status_t prepared = jb_temp_dir_create(work, &scratch);
  expect(prepared == HAL_OK, "project types", "create scratch");
  if (prepared == HAL_OK) {
    const char *directory = jb_temp_dir_path(scratch);
    jb_string_t binding = {0};
    (void)jb_string_init(&binding, jb_heap_allocator());
    if (!read_fixture(registry, "bindings/gpio-output.json", &binding))
      prepared = HAL_EIO;
    const char *path = jh_hw_path(h, directory, "led.json");
    if (prepared == HAL_OK)
      prepared = write_text(path, jh_string_span(&binding));
    if (prepared == HAL_OK)
      prepared = write_text(jh_hw_path(h, directory, "second.json"),
                            jh_string_span(&binding));
    jb_json_tree_t *shadow = jh_hw_parse_tree(h, jh_string_span(&binding));
    jh_hw_set(h, shadow, "compatible",
              jh_hw_string(h, jh_span("jaszczurhal,gpio-output")));
    jb_string_clear(&binding);
    jh_node_storage(&h->memory, jh_hw_format(jh_hw_root(shadow), 0, &binding));
    if (prepared == HAL_OK)
      prepared = write_text(jh_hw_path(h, directory, "shadow.json"),
                            jh_string_span(&binding));
    if (prepared == HAL_OK)
      prepared = jb_fs_mkdir(jh_hw_path(h, directory, "sub"), false);
    if (prepared == HAL_OK)
      prepared =
          write_text(jh_hw_path(h, directory, "broken.json"), jh_span("{"));
    static const struct {
      const char *entries, *pointer;
      bool sub;
    } cases[] = {
        {"[\"../led.json\",\".././led.json\"]", NULL, true},
        {"[\"led.json\",\"second.json\"]", "/compatible", false},
        {"[\"shadow.json\"]", "/compatible", false},
        {"[\"/abs/led.json\"]", "/bindings/0", false},
        {"[\"https://example.invalid/led.json\"]", "/bindings/0", false},
        {"[\"sub\\\\led.json\"]", "/bindings/0", false},
        {"[\"*.json\"]", "/bindings/0", false},
        {"[\"missing.json\"]", "/bindings/0", false},
        {"[\"broken.json\",\"missing.json\"]", "/bindings/1", false}};
    expect(prepared == HAL_OK && h->memory.status == HAL_OK, "project types",
           "prepare bindings");
    for (size_t i = 0;
         prepared == HAL_OK && h->memory.status == HAL_OK && i < COUNTOF(cases);
         ++i) {
      jh_catalogue_t types = {0};
      jh_json_location_t where;
      jh_json_location_init(&where);
      jh_failure_t error;
      jh_failure_init(&error);
      jh_json_document_t document = {0};
      const jb_json_node_t entries = jh_hw_document(
          h, jh_hw_root(jh_hw_parse_tree(h, jh_span(cases[i].entries))),
          &document);
      hal_status_t status = jh_catalogue_init(&types);
      if (status == HAL_OK)
        status = jh_catalogue_borrow(&types, &registry->catalogue);
      const char *base =
          cases[i].sub ? jh_hw_path(h, directory, "sub") : directory;
      if (status == HAL_OK)
        status = jh_catalogue_load_project(
            &types, &where, jb_string_data(&registry->schema_dir), base,
            jh_hw_path(h, base, "device_tree.json"), entries,
            registry->max_bytes, &error);
      if (cases[i].pointer == NULL) {
        expect(status == HAL_OK &&
                   jb_vector_size(&types.bindings) ==
                       jb_vector_size(&registry->catalogue.bindings) + 1,
               cases[i].entries, "same real file loaded once");
      } else {
        expect(status == HAL_ECONFIG &&
                   strcmp(jb_string_data(&error.code), "JH-HW-BINDING") == 0 &&
                   strcmp(jb_string_data(&error.pointer), cases[i].pointer) ==
                       0,
               cases[i].entries, jb_string_data(&error.message));
        if (i == 2)
          expect(strstr(jb_string_data(&error.detail),
                        jh_hw_path(h, jb_string_data(&registry->root),
                                   "config/hardware/bindings/"
                                   "jaszczurhal-gpio-output.json")) != NULL,
                 "shadow", jb_string_data(&error.detail));
      }
      jh_catalogue_destroy(&types);
      jh_json_document_destroy(&document);
      jh_failure_destroy(&error);
      jh_json_location_destroy(&where);
    }
    jb_string_destroy(&binding);
    const char *invalid_feature = jh_hw_path(h, directory, "feature.json");
    const char *invalid_default = jh_hw_path(h, directory, "default.json");
    const char *device_tree = jh_hw_path(h, directory, "device_tree.json");
    expect(
        write_text(invalid_feature,
                   jh_span("{\"schemaVersion\":1,\"compatible\":\"test,"
                           "feature\",\"domain\":\"gpio\","
                           "\"description\":\"Unknown "
                           "feature.\",\"requiresFeatures\":[\"HAL_ENABLE_NO_"
                           "SUCH_FEATURE\"]}")) == HAL_OK &&
            write_text(
                invalid_default,
                jh_span(
                    "{\"schemaVersion\":1,\"compatible\":\"test,default\","
                    "\"domain\":\"gpio\","
                    "\"description\":\"Out of "
                    "range.\",\"properties\":{\"level\":{\"type\":\"integer\","
                    "\"cType\":\"uint8\",\"unit\":\"1\",\"description\":"
                    "\"Level.\","
                    "\"default\":300}}}")) == HAL_OK &&
            write_text(device_tree,
                       jh_span("{\"schemaVersion\":1,\"bindings\":[\"feature."
                               "json\",\"default.json\"],"
                               "\"nodes\":{},\"assemblies\":{\"pico\":{"
                               "\"target\":\"rp2040\",\"board\":\"pico\","
                               "\"clock\":{\"tree\":\"default\"},\"statusLed\":"
                               "{\"node\":\"board.statusLed\"},"
                               "\"nodes\":{}}}}")) == HAL_OK,
        "catalogue phases", "prepare conflicting errors");
    jh_hardware_t hardware = {0};
    hal_status_t resolved =
        resolve(registry, device_tree, "pico", NULL, &hardware, &failure);
    expect(resolved == HAL_ECONFIG &&
               strcmp(jb_string_data(&failure.pointer),
                      "/properties/level/default") == 0 &&
               strcmp(jb_string_data(&failure.source), invalid_default) == 0,
           "catalogue phases", jb_string_data(&failure.message));
    jh_hardware_destroy(&hardware);
    jh_failure_destroy(&failure);
    jh_failure_init(&failure);
    expect(
        write_text(
            device_tree,
            jh_span(
                "{\"schemaVersion\":1,\"nodes\":{\"led\":{\"compatible\":"
                "\"jaszczurhal,gpio-output\"}},"
                "\"assemblies\":{\"zero\":{\"target\":\"rp2040\",\"board\":"
                "\"rp2040-zero\","
                "\"clock\":{\"tree\":\"default\"},\"statusLed\":{\"node\":"
                "\"led\"},"
                "\"releaseReservations\":[\"status-led\"],\"nets\":{\"blue\":{"
                "\"endpoint\":{"
                "\"domain\":\"soc-gpio\",\"id\":16},\"owner\":\"board.data\"}},"
                "\"nodes\":{\"led\":{\"present\":true,\"pins\":{\"out\":14}}}}}"
                "}")) == HAL_OK,
        "board owner", "prepare invalid owner");
    resolved =
        resolve(registry, device_tree, "zero", NULL, &hardware, &failure);
    expect(resolved == HAL_ECONFIG &&
               strcmp(jb_string_data(&failure.code), "JH-HW-PIN-RANGE") == 0 &&
               strcmp(jb_string_data(&failure.pointer),
                      "/assemblies/zero/nets/blue/endpoint") == 0,
           "board owner", jb_string_data(&failure.message));
    jh_hardware_destroy(&hardware);
    jh_failure_destroy(&failure);
    jh_failure_init(&failure);
  }
  expect(jb_temp_dir_remove(&scratch) == HAL_OK, "project types",
         "scratch cleanup");
  expect(h->memory.status == HAL_OK, "project types",
         jb_string_data(&failure.message));
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void input_cases(const jh_hardware_registry_t *registry,
                        const char *work) {
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_json_location_t where;
  jh_json_location_init(&where);
  jb_string_t path = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  expect(jh_path_join(work, "hardware-input.json", &path) == HAL_OK, "reader",
         "input path");
  static const struct {
    const char *json, *detail;
  } cases[] = {{"{\"a\":1,\"a\":2}", ""},
               {"{\"a\":Infinity}", ""},
               {"{\"a\":[1e400]}", "binary64"},
               {"{\"a\":[-1e400]}", "binary64"},
               {"{\"a\":[1e-400]}", "binary64"},
               {"{\"a\":[-2.5e-999]}", "binary64"},
               {"{\"a\":[4.9e-324,0e400,-0.0,1.7976931348623157e308]}", NULL},
               {"{\"a\":[\"\\ud800\"]}", ""},
               {"{\"a\":\"\\ud83d\\ude00\"}", NULL},
               {"-Infinity", ""},
               {"NaN", ""}};
  for (size_t i = 0; i < COUNTOF(cases); ++i) {
    expect(write_text(jb_string_data(&path), jh_span(cases[i].json)) == HAL_OK,
           "reader", "write input");
    jh_json_document_t document = {0};
    jh_failure_destroy(&failure);
    jh_failure_init(&failure);
    const hal_status_t status =
        jh_json_read_document(&where, jb_string_data(&path),
                              registry->max_bytes, true, &document, &failure);
    expect(cases[i].detail == NULL ? status == HAL_OK : status == HAL_EPROTO,
           cases[i].json, jb_string_data(&failure.message));
    if (cases[i].detail != NULL)
      expect(strstr(jb_string_data(&failure.message), cases[i].detail) != NULL,
             cases[i].json, "reader failure detail");
    else if (i == 8) {
      jb_json_node_t value = {0};
      char decoded[8];
      size_t required = 0;
      expect(jb_json_find((jb_json_node_t){&document.doc, 0}, jh_span("a"),
                          &value) == HAL_OK &&
                 jb_json_string_copy(value, decoded, sizeof decoded,
                                     &required) == HAL_OK &&
                 required == 4 && memcmp(decoded, "\xf0\x9f\x98\x80", 4) == 0,
             "reader", "paired surrogate decodes to UTF-8");
    }
    if (i == 7)
      expect(where.located && where.error.code == JB_JSON_ERROR_ESCAPE,
             "reader", "unpaired surrogate is an escape error");
    jh_json_document_destroy(&document);
  }
  expect(jh_json_pointer_compare(jh_span("/a/9"), jh_span("/a/10")) < 0 &&
             jh_json_pointer_compare(jh_span("/a/10"),
                                     jh_span("/nodes/fooBAR")) < 0 &&
             jh_json_pointer_compare(jh_span("/nodes/fooBAR"),
                                     jh_span("/nodes/fooBar")) < 0,
         "pointers", "numeric indices and code point ordering");
  expect(jb_fs_remove(jb_string_data(&path), false) == HAL_OK, "reader",
         "remove input");
  jb_string_destroy(&path);
  jh_json_location_destroy(&where);
  jh_failure_destroy(&failure);
}

static void literal_case(const jh_hardware_registry_t *registry,
                         jb_json_tree_view_t test) {
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  const char *type = jh_hw_text(h, jh_json_field(test, "cType"));
  jb_json_tree_view_t input = jh_json_field(test, "input");
  if (jh_hw_is(jh_json_field(test, "inputEncoding"), "json-number-token"))
    input = jh_hw_root(jh_hw_parse_tree(h, jh_span(jh_hw_text(h, input))));
  const char *actual = jh_hw_literal(h, type, input);
  if (jh_json_field(test, "error").tree != NULL) {
    expect(h->memory.status == HAL_ECONFIG, type, "numeric value rejected");
    expect(jb_string_size(&failure.message) != 0, type,
           "literal failure explains the rejected value");
  } else {
    expect(h->memory.status == HAL_OK &&
               text_is(jh_json_field(test, "literal"), actual),
           type, actual);
    if (jh_json_field(test, "hashValue").tree != NULL) {
      jb_json_tree_t *typed = jh_hw_object(h);
      jh_hw_set(h, typed, "cType", jh_hw_string(h, jh_span(type)));
      jh_hw_set(h, typed, "value", input);
      const jb_json_tree_view_t encoded =
          jh_hw_serialized(h, jh_hw_root(typed), true);
      expect(jh_hw_equal(h, jh_json_field(test, "hashValue"),
                         jh_json_field(encoded, "value")),
             type, "IEEE bits of exact decimal");
    }
  }
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void literal_cases(const jh_hardware_registry_t *registry,
                          jb_json_tree_view_t numeric) {
  /* Ported from test_hardware_literals.py. JsonNumber's Python inheritance
   * and pickle behavior disappear; its exact token contract remains below. */
  static const char cases[] =
      "[{\"cType\":\"bool\",\"input\":true,\"literal\":\"1\"},"
      "{\"cType\":\"bool\",\"input\":false,\"literal\":\"0\"},"
      "{\"cType\":\"uint8\",\"input\":255,\"literal\":\"255U\"},"
      "{\"cType\":\"uint32\",\"input\":2.0,\"literal\":\"2U\"},"
      "{\"cType\":\"uint64\",\"input\":18446744073709551615,\"literal\":"
      "\"18446744073709551615ULL\"},"
      "{\"cType\":\"int8\",\"input\":-9,\"literal\":\"(-9)\"},"
      "{\"cType\":\"int16\",\"input\":7,\"literal\":\"7\"},"
      "{\"cType\":\"int32\",\"input\":-2147483648,\"literal\":\"(-2147483647 - "
      "1)\"},"
      "{\"cType\":\"int64\",\"input\":-5,\"literal\":\"(-5LL)\"},"
      "{\"cType\":\"int64\",\"input\":-9223372036854775808,\"literal\":\"(-"
      "9223372036854775807LL - 1LL)\"},"
      "{\"cType\":\"uint64\",\"input\":1.8446744073709551615e19,\"literal\":"
      "\"18446744073709551615ULL\"},"
      "{\"cType\":\"string\",\"input\":\"a\\\"b\\\\c\\nµA?\","
      "\"literal\":\"\\\"a\\\\\\\"b\\\\\\\\c\\\\012\\\\302\\\\265A\\\\?\\\"\"},"
      "{\"cType\":\"float\",\"input\":1.000000059604644775390625,\"literal\":"
      "\"1.00000000e+00f\",\"hashValue\":\"3f800000\"},"
      "{\"cType\":\"float\",\"input\":1.000000178813934326171875,\"literal\":"
      "\"1.00000024e+00f\",\"hashValue\":\"3f800002\"},"
      "{\"cType\":\"float\",\"input\":-0.22,\"literal\":\"(-2.19999999e-01f)\"}"
      ","
      "{\"cType\":\"double\",\"input\":-1,\"literal\":\"(-1.0000000000000000e+"
      "00)\"},"
      "{\"cType\":\"double\",\"input\":-0e5,\"literal\":\"0.0000000000000000e+"
      "00\",\"hashValue\":\"0000000000000000\"},"
      "{\"cType\":\"double\",\"input\":1,\"literal\":\"1.0000000000000000e+"
      "00\"},"
      "{\"cType\":\"float\",\"input\":3.4028235e38,\"literal\":\"3.40282347e+"
      "38f\",\"hashValue\":\"7f7fffff\"},"
      "{\"cType\":\"float\",\"input\":1e-45,\"literal\":\"1.40129846e-45f\","
      "\"hashValue\":\"00000001\"},"
      "{\"cType\":\"double\",\"input\":4e-324,\"literal\":\"4."
      "9406564584124654e-324\",\"hashValue\":\"0000000000000001\"},"
      "{\"cType\":\"double\",\"input\":1.7976931348623157e308,\"literal\":\"1."
      "7976931348623157e+308\",\"hashValue\":\"7fefffffffffffff\"},"
      "{\"cType\":\"float\",\"input\":3.5e38,\"error\":\"JH-HW-PROPERTY\"},"
      "{\"cType\":\"float\",\"input\":7e-46,\"error\":\"JH-HW-PROPERTY\"},"
      "{\"cType\":\"double\",\"input\":2e-324,\"error\":\"JH-HW-PROPERTY\"}]";
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  const jb_json_tree_view_t explicit_cases =
      jh_hw_root(jh_hw_parse_tree(h, jh_span(cases)));
  expect(h->memory.status == HAL_OK, "literals", "parse ported cases");
  const jb_json_tree_view_t tables[] = {numeric, explicit_cases};
  for (size_t i = 0; i < COUNTOF(tables); ++i)
    for (size_t j = 0; j < jh_json_count(tables[i]); ++j)
      literal_case(registry, jh_json_item(tables[i], j));
  static const char *const tokens[][2] = {
      {"radioModule.radio", "RADIO_MODULE_RADIO"},
      {"fooBar", "FOO_BAR"},
      {"fooBAR", "FOO_BAR"},
      {"waveshare,core1262-hf", "WAVESHARE_CORE1262_HF"},
      {"1v8", "1V8"}};
  for (size_t i = 0; i < COUNTOF(tokens); ++i)
    expect(strcmp(jh_hw_token(h, tokens[i][0]), tokens[i][1]) == 0,
           tokens[i][0], "macro token");
  const jb_json_tree_view_t exact =
      jh_hw_root(jh_hw_parse_tree(h, jh_span("[0.22000000000000000,2,1e2]")));
  const jb_json_tree_view_t copy = jh_hw_root(jh_hw_copy(h, exact));
  static const char *const expected[] = {"0.22000000000000000", "2", "1e2"};
  for (size_t i = 0; i < COUNTOF(expected); ++i) {
    jb_json_span_t token = {0};
    expect(jb_json_tree_text(jh_json_item(copy, i), &token) == HAL_OK &&
               jh_span_equal(token, jh_span(expected[i])),
           "JSON numbers", "copy keeps exact token");
  }
  const jb_json_tree_view_t raw_text = jh_hw_root(
      jh_hw_parse_tree(h, jh_span("{\"text\":\"\\u0000raw:1\",\"nested\":[{"
                                  "\"x\":\"\\u0000raw:2.5e+00\"}],\"n\":7}")));
  expect(jh_hw_equal(h, raw_text, jh_hw_serialized(h, raw_text, false)),
         "serialization", "text never becomes a number");
  static const char *const typed[] = {
      "{\"p\":{\"value\":0.250,\"cType\":\"float\",\"unit\":\"ohm\"}}",
      "{\"p\":{\"value\":0.25,\"cType\":\"float\",\"unit\":\"ohm\"}}",
      "{\"p\":{\"value\":0.25,\"cType\":\"double\",\"unit\":\"ohm\"}}"};
  char digests[COUNTOF(typed)][65] = {{0}};
  for (size_t i = 0; i < COUNTOF(typed); ++i)
    jh_hw_digest(h, jh_hw_root(jh_hw_parse_tree(h, jh_span(typed[i]))),
                 digests[i]);
  expect(strcmp(digests[0], digests[1]) == 0 &&
             strcmp(digests[1], digests[2]) != 0,
         "digest", "exact decimals hash by their declared IEEE representation");
  expect(h->memory.status == HAL_OK, "literals",
         jb_string_data(&failure.message));
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void value_cases(const jh_hardware_registry_t *registry) {
  static const char values[] =
      "[[null,null,true],[true,1,false],[1,1.0,true],[-0,0e100,true],"
      "[9223372036854775807,9.223372036854775807e18,true],"
      "[9007199254740992,9007199254740993,false],"
      "[\"a\\u0000x\",\"a\\u0000y\",false],"
      "[\"a\\u0000x\",\"a\\u0000x\",true]]";
  static const char endpoints[] =
      "[[{\"domain\":\"soc-gpio\",\"id\":1},"
      "{\"domain\":\"soc-gpio\",\"id\":1e0},true],"
      "[{\"domain\":\"soc-gpio\",\"id\":1},"
      "{\"domain\":\"soc-gpio\",\"id\":\"1\"},false],"
      "[{\"domain\":\"device-pin\",\"node\":\"bank\",\"index\":2},"
      "{\"domain\":\"device-pin\",\"node\":\"bank\",\"index\":2.0},true],"
      "[{\"domain\":\"device-pin\",\"node\":\"a\",\"index\":2},"
      "{\"domain\":\"device-pin\",\"node\":\"b\",\"index\":2},false],"
      "[{\"domain\":\"component-gpio\",\"component\":\"radio\",\"id\":0,"
      "\"halPin\":64},"
      "{\"domain\":\"component-gpio\",\"component\":\"radio\",\"id\":0},true],"
      "[{\"domain\":\"component-gpio\",\"component\":\"a\",\"id\":0},"
      "{\"domain\":\"component-gpio\",\"component\":\"b\",\"id\":0},false],"
      "[{\"domain\":\"soc-gpio\",\"id\":0},"
      "{\"domain\":\"component-gpio\",\"component\":\"radio\",\"id\":0},false],"
      "[null,null,false]]";
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  const char *const tables[] = {values, endpoints};
  for (size_t table = 0; table < COUNTOF(tables); ++table) {
    const jb_json_tree_view_t cases =
        jh_hw_root(jh_hw_parse_tree(h, jh_span(tables[table])));
    for (size_t i = 0; i < jh_json_count(cases); ++i) {
      const jb_json_tree_view_t row = jh_json_item(cases, i);
      const bool same =
          table == 0
              ? jh_hw_equal(h, jh_json_item(row, 0), jh_json_item(row, 1))
              : jh_hw_endpoint_equal(h, jh_json_item(row, 0),
                                     jh_json_item(row, 1));
      expect(same == jh_hw_bool(jh_json_item(row, 2)), "value comparison",
             table == 0 ? "exact scalar equality"
                        : "physical endpoint identity");
    }
  }
  const jb_json_tree_view_t original = jh_hw_root(jh_hw_parse_tree(
      h, jh_span("{\"z\\u0000tail\":[1e2,{\"n\":18446744073709551615,"
                 "\"s\":\"a\\u0000b\"}],\"a\":true}")));
  jh_hw_container_t object;
  jh_hw_container_init(h, &object, true);
  for (size_t i = 0; i < jh_json_count(original); ++i) {
    const jb_json_tree_view_t item = jh_json_item(original, i);
    jh_hw_container_put(h, &object, jh_json_key(item), item);
  }
  const jb_json_tree_view_t copied = jh_hw_container_finish(h, &object);
  expect(jh_span_equal(jh_json_key(jh_json_item(copied, 0)),
                       jh_json_key(jh_json_item(original, 0))),
         "bulk container", "insertion order and embedded NUL key");
  jb_string_t before = {0}, after = {0};
  (void)jb_string_init(&before, jb_heap_allocator());
  (void)jb_string_init(&after, jb_heap_allocator());
  expect(jh_hw_format(original, 0, &before) == HAL_OK &&
             jh_hw_format(copied, 0, &after) == HAL_OK &&
             jh_span_equal(jh_string_span(&before), jh_string_span(&after)),
         "bulk container",
         "exact numbers and escaped text survive construction");
  jb_string_destroy(&after);
  jb_string_destroy(&before);
  /* A generated object with common-prefix names exhausted the former budget. */
  jh_hw_container_init(h, &object, true);
  const jb_json_tree_view_t enabled = jh_hw_boolean(h, true);
  for (size_t i = 0; i < 160; ++i) {
    char key[64];
    (void)snprintf(key, sizeof key, "member_with_common_prefix_%zu", i);
    jh_hw_container_put(h, &object, jh_span(key), enabled);
  }
  const jb_json_tree_view_t wide = jh_hw_container_finish(h, &object);
  expect(jh_json_count(wide) == 160 &&
             jh_hw_bool(jh_json_field(wide, "member_with_common_prefix_159")),
         "bulk container",
         "common-prefix names fit the generated object budget");
  expect(h->memory.status == HAL_OK, "value cases",
         jb_string_data(&failure.message));
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void large_tree_cases(const jh_hardware_registry_t *registry,
                             const char *work) {
  static const size_t sizes[] = {160, 320, 2048};
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  const char *directory = jh_hw_path(h, work, "large-tree");
  const char *path = jh_hw_path(h, directory, "device_tree.json");
  hal_status_t status = write_text(
      jh_hw_path(h, directory, "bank.json"),
      jh_span("{\"schemaVersion\":1,\"compatible\":\"test,bank\","
              "\"description\":\"Synthetic pin bank.\",\"domain\":\"expander\","
              "\"pinCount\":4096}"));
  for (size_t n = 0; status == HAL_OK && n < COUNTOF(sizes); ++n) {
    jb_string_t json = {0};
    (void)jb_string_init(&json, jb_heap_allocator());
    jh_put(&json, &status,
           "{\"schemaVersion\":1,\"bindings\":[\"bank.json\"],"
           "\"nodes\":{\"bank\":{\"compatible\":\"test,bank\"}");
    for (size_t i = 0; i < sizes[n]; ++i) {
      jh_put(&json, &status, ",\"n");
      jh_put_int64(&json, &status, (int64_t)i);
      jh_put(&json, &status, "\":{\"compatible\":\"jaszczurhal,gpio-output\"}");
    }
    jh_put(&json, &status,
           "},\"assemblies\":{\"pico\":{\"target\":\"rp2040\","
           "\"board\":\"pico\",\"clock\":{\"tree\":\"default\"},"
           "\"statusLed\":{\"node\":\"board.statusLed\"},\"nodes\":{"
           "\"bank\":{\"present\":true}");
    for (size_t i = 0; i < sizes[n]; ++i) {
      jh_put(&json, &status, ",\"n");
      jh_put_int64(&json, &status, (int64_t)i);
      jh_put(&json, &status,
             "\":{\"present\":true,\"pins\":{\"out\":{"
             "\"domain\":\"device-pin\",\"node\":\"bank\",\"index\":");
      jh_put_int64(&json, &status, (int64_t)i);
      jh_put(&json, &status, "}}}");
    }
    jh_put(&json, &status, "}}}}");
    if (status == HAL_OK)
      status = write_text(path, jh_string_span(&json));
    jh_hardware_t hardware = {0};
    if (status == HAL_OK) {
      const hal_status_t loaded =
          resolve(registry, path, "pico", NULL, &hardware, &failure);
      expect(loaded == HAL_OK, "large device tree",
             jb_string_data(&failure.message));
    }
    jh_hardware_destroy(&hardware);
    jh_failure_destroy(&failure);
    jh_failure_init(&failure);
    if (n == 0 && status == HAL_OK) {
      char *last = strstr(jb_string_mutable_data(&json), "\"index\":159");
      expect(last != NULL, "large device tree", "locate last pin");
      if (last != NULL) {
        memcpy(last + 8, "0e0", 3);
        status = write_text(path, jh_string_span(&json));
        const hal_status_t conflicted =
            resolve(registry, path, "pico", NULL, &hardware, &failure);
        expect(conflicted == HAL_ECONFIG &&
                   strcmp(jb_string_data(&failure.code),
                          "JH-HW-PIN-CONFLICT") == 0 &&
                   strcmp(jb_string_data(&failure.related),
                          "/assemblies/pico/nodes/n0/pins/out") == 0,
               "large device tree", "numeric alias retains the first claimant");
        jh_hardware_destroy(&hardware);
        jh_failure_destroy(&failure);
        jh_failure_init(&failure);
      }
    }
    jb_string_destroy(&json);
  }
  expect(status == HAL_OK, "large device tree", "prepare input");
  expect(jb_fs_remove(directory, true) == HAL_OK, "large device tree",
         "cleanup");
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
}

static void process_cases(const jh_hardware_registry_t *registry,
                          const char *work) {
#ifndef _WIN32
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hw_t *h = test_context(registry, &failure);
  if (h == NULL) {
    jh_failure_destroy(&failure);
    return;
  }
  const char *directory = jh_hw_path(h, work, "process-paths");
  const char *driver = jh_hw_path(h, directory, "driver");
  const char *compiler = jh_hw_path(h, directory, "cc");
  const char *cycle = jh_hw_path(h, directory, "cycle");
  const char *missing = jh_hw_path(h, directory, "missing");
  jb_string_t out = {0}, old_path = {0}, old_tmp = {0};
  (void)jb_string_init(&out, jb_heap_allocator());
  (void)jb_string_init(&old_path, jb_heap_allocator());
  (void)jb_string_init(&old_tmp, jb_heap_allocator());
  bool had_path = false, had_tmp = false;
  expect(jh_process_environment("PATH", &old_path, &had_path) == HAL_OK &&
             jh_process_environment("TMPDIR", &old_tmp, &had_tmp) == HAL_OK,
         "process", "save environment");
  expect(write_text(driver, jh_span("")) == HAL_OK && chmod(driver, 0700) == 0,
         "process", "create executable fixture");
  (void)jb_fs_remove(compiler, false);
  (void)jb_fs_remove(cycle, false);
  expect(symlink("driver", compiler) == 0 && symlink("cycle", cycle) == 0,
         "process", "create owned executable links");
  const char *const names[] = {compiler, "./cc"};
  const char *const expected[] = {
      compiler, jh_hw_join(h, JH_PARTS(jh_span(directory), jh_span("/./cc")))};
  for (size_t i = 0; i < COUNTOF(names); ++i) {
    expect(jh_process_executable(names[i], directory, false, &out, &failure) ==
                   HAL_OK &&
               strcmp(jb_string_data(&out), expected[i]) == 0,
           "process", "preserve compiler symlink name");
    jh_failure_destroy(&failure);
    jh_failure_init(&failure);
  }
  const char *nested = jh_hw_path(h, directory, "actual/nested");
  const char *parent_tool = jh_hw_path(h, directory, "actual/parent-tool");
  const char *alias = jh_hw_path(h, directory, "alias");
  const char *parent_path =
      jh_hw_join(h, JH_PARTS(jh_span(alias), jh_span("/..")));
  const char *tool_path =
      jh_hw_join(h, JH_PARTS(jh_span(parent_path), jh_span("/parent-tool")));
  expect(jb_fs_mkdir(nested, true) == HAL_OK &&
             symlink(driver, parent_tool) == 0 &&
             symlink("actual/nested", alias) == 0,
         "process", "create parent symlink fixture");
  expect(jh_process_executable(tool_path, directory, false, &out, &failure) ==
                 HAL_OK &&
             access(jb_string_data(&out), X_OK) == 0,
         "process", "resolve parent link before dot-dot");
  jh_failure_destroy(&failure);
  jh_failure_init(&failure);
  expect(setenv("PATH", parent_path, 1) == 0, "process",
         "set parent link PATH");
  for (size_t i = 0; i < 2; ++i) {
    expect(jh_process_executable("parent-tool", directory, i != 0, &out,
                                 &failure) == HAL_OK &&
               access(jb_string_data(&out), X_OK) == 0,
           "process", "preserve parent link in PATH");
    jh_failure_destroy(&failure);
    jh_failure_init(&failure);
  }
  const char *search =
      jh_hw_join(h, JH_PARTS(jh_span(cycle), jh_span(":"), jh_span(directory)));
  expect(setenv("PATH", search, 1) == 0, "process", "set test PATH");
  for (size_t i = 0; i < 2; ++i) {
    expect(jh_process_executable("cc", directory, i != 0, &out, &failure) ==
                   HAL_OK &&
               strcmp(jb_string_data(&out), compiler) == 0,
           "process", "skip a cyclic PATH entry");
    jh_failure_destroy(&failure);
    jh_failure_init(&failure);
  }
  expect(setenv("TMPDIR", missing, 1) == 0, "process", "set missing TMPDIR");
  jb_temp_dir_t *scratch = NULL;
  const hal_status_t scratch_status = jh_process_scratch(&scratch, &failure);
  expect(scratch_status == HAL_OK && scratch != NULL, "process TMPDIR fallback",
         jb_string_data(&failure.message));
  if (scratch != NULL)
    expect(jb_temp_dir_remove(&scratch) == HAL_OK, "process", "remove scratch");
  expect((had_path ? setenv("PATH", jb_string_data(&old_path), 1)
                   : unsetenv("PATH")) == 0 &&
             (had_tmp ? setenv("TMPDIR", jb_string_data(&old_tmp), 1)
                      : unsetenv("TMPDIR")) == 0,
         "process", "restore environment");
  expect(jb_fs_remove(directory, true) == HAL_OK, "process", "remove fixtures");
  jb_string_destroy(&old_tmp);
  jb_string_destroy(&old_path);
  jb_string_destroy(&out);
  jh_hardware_t owner = {h};
  jh_hardware_destroy(&owner);
  jh_failure_destroy(&failure);
#else
  (void)registry;
  (void)work;
#endif
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3 && argc != 5) {
    (void)fprintf(stderr, "usage: test_hardware ROOT [WORK | --model|--header "
                          "DEVICE_TREE ASSEMBLY]\n");
    return 2;
  }
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_hardware_registry_t registry;
  jh_hardware_registry_init(&registry);
  hal_status_t status = jh_hardware_registry_load(
      &registry, argv[1], 16u * 1024u * 1024u, &failure);
  if (argc == 5 && status == HAL_OK) {
    jh_hardware_t hardware = {0};
    status = resolve(&registry, argv[3], argv[4], NULL, &hardware, &failure);
    if (status == HAL_OK) {
      jb_string_t json = {0};
      (void)jb_string_init(&json, jb_heap_allocator());
      status = strcmp(argv[2], "--header") == 0
                   ? jh_hardware_header(&hardware, &json, &failure)
               : strcmp(argv[2], "--document") == 0
                   ? jh_hardware_document(&hardware, &json, &failure)
                   : jh_hardware_json(&hardware, &json, &failure);
      if (status == HAL_OK)
        status =
            stdout_sink(NULL, jb_string_data(&json), jb_string_size(&json));
      jb_string_destroy(&json);
    }
    jh_hardware_destroy(&hardware);
  } else if (status == HAL_OK) {
    jb_string_t fixtures = {0}, path = {0};
    (void)jb_string_init(&fixtures, jb_heap_allocator());
    (void)jb_string_init(&path, jb_heap_allocator());
    status = jh_path_join(argv[1], "tests/fixtures/device_tree", &fixtures);
    if (status == HAL_OK)
      status = jh_path_join(jb_string_data(&fixtures), "cases.json", &path);
    jb_json_tree_t *tree = NULL;
    jb_json_tree_view_t root = {0};
    if (status == HAL_OK)
      status = jh_check_load_object(jb_string_data(&path), 16u * 1024u * 1024u,
                                    &tree, &root, &failure);
    if (status == HAL_OK)
      specification(&registry, jb_string_data(&fixtures),
                    jh_json_field(root, "cases"));
    if (status == HAL_OK) {
      selection_cases(&registry, jb_string_data(&fixtures),
                      jh_json_field(root, "selectionCases"));
      resolution_cases(&registry, jb_string_data(&fixtures),
                       jh_json_field(root, "resolutionCases"));
      literal_cases(&registry, jh_json_field(root, "numericCases"));
      value_cases(&registry);
      renderer_cases(&registry, jh_json_field(root, "rendererCases"));
      board_source_case(&registry);
    }
    if (status == HAL_OK && argc == 3)
      i2c_cases(&registry, argv[2], jh_json_field(root, "i2cAddressCases"));
    if (status == HAL_OK && argc == 3)
      finalize_cases(&registry, argv[2], root);
    if (status == HAL_OK && argc == 3)
      effective_cases(&registry, argv[2]);
    if (status == HAL_OK && argc == 3)
      assembly_effective_cases(&registry, argv[2]);
    if (status == HAL_OK && argc == 3) {
      catalogue_cases(&registry, argv[2]);
      input_cases(&registry, argv[2]);
      process_cases(&registry, argv[2]);
      large_tree_cases(&registry, argv[2]);
    }
    jb_json_tree_destroy(tree);
    jb_string_destroy(&path);
    jb_string_destroy(&fixtures);
    (void)printf("%zu checks, %zu failures\n", checks, failures);
  }
  if (status != HAL_OK)
    (void)fprintf(stderr, "%s (%s)\n", jb_string_data(&failure.message),
                  hal_status_to_string(status));
  jh_hardware_registry_destroy(&registry);
  jh_failure_destroy(&failure);
  return status == HAL_OK && failures == 0 ? 0 : 1;
}

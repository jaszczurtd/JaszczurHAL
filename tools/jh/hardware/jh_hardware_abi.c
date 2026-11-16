#include "jh_hardware_internal.h"
#include "jh_process_support.h"
#include <jb/process.h>
#include <string.h>

static const char *const abi_macros[] = {"__ARM_PCS",
                                         "__ARM_PCS_VFP",
                                         "__SOFTFP__",
                                         "__ARM_FP",
                                         "__ARM_ARCH",
                                         "__ARM_ARCH_PROFILE",
                                         "__ARM_EABI__",
                                         "__ARM_SIZEOF_WCHAR_T",
                                         "__ARM_SIZEOF_MINIMAL_ENUM",
                                         "__thumb__",
                                         "__thumb2__",
                                         "__riscv_xlen",
                                         "__riscv_flen",
                                         "__riscv_float_abi_soft",
                                         "__riscv_float_abi_single",
                                         "__riscv_float_abi_double",
                                         "__riscv_float_abi_quad",
                                         "__riscv_abi_rve",
                                         "__XTENSA_CALL0_ABI__",
                                         "__XTENSA_WINDOWED_ABI__",
                                         "__SIZEOF_SHORT__",
                                         "__SIZEOF_INT__",
                                         "__SIZEOF_LONG__",
                                         "__SIZEOF_LONG_LONG__",
                                         "__SIZEOF_POINTER__",
                                         "__SIZEOF_SIZE_T__",
                                         "__SIZEOF_PTRDIFF_T__",
                                         "__SIZEOF_WCHAR_T__",
                                         "__SIZEOF_FLOAT__",
                                         "__SIZEOF_DOUBLE__",
                                         "__SIZEOF_LONG_DOUBLE__",
                                         "__BYTE_ORDER__",
                                         "__CHAR_UNSIGNED__"};

static const struct {
  const char *name, *declaration, *expression;
} probes[] = {
    {"member_short", "struct jh_abi_layout_member_short { char c; short x; };",
     "offsetof(struct jh_abi_layout_member_short, x)"},
    {"member_int", "struct jh_abi_layout_member_int { char c; int x; };",
     "offsetof(struct jh_abi_layout_member_int, x)"},
    {"member_long", "struct jh_abi_layout_member_long { char c; long x; };",
     "offsetof(struct jh_abi_layout_member_long, x)"},
    {"member_long_long",
     "struct jh_abi_layout_member_long_long { char c; long long x; };",
     "offsetof(struct jh_abi_layout_member_long_long, x)"},
    {"member_pointer",
     "struct jh_abi_layout_member_pointer { char c; void * x; };",
     "offsetof(struct jh_abi_layout_member_pointer, x)"},
    {"member_float", "struct jh_abi_layout_member_float { char c; float x; };",
     "offsetof(struct jh_abi_layout_member_float, x)"},
    {"member_double",
     "struct jh_abi_layout_member_double { char c; double x; };",
     "offsetof(struct jh_abi_layout_member_double, x)"},
    {"member_long_double",
     "struct jh_abi_layout_member_long_double { char c; long double x; };",
     "offsetof(struct jh_abi_layout_member_long_double, x)"},
    {"struct_char", "struct jh_abi_layout_struct_char { char c; };",
     "sizeof(struct jh_abi_layout_struct_char)"},
    {"struct_bitfield",
     "struct jh_abi_layout_struct_bitfield { char c; unsigned int x : 4; };",
     "sizeof(struct jh_abi_layout_struct_bitfield)"},
    {"enum", "enum jh_abi_layout_enum { JH_ABI_LAYOUT_ENUM = 1 };",
     "sizeof(enum jh_abi_layout_enum)"}};

typedef struct {
  jb_string_t output, error;
} capture_t;

static hal_status_t capture(void *context, jb_process_stream_t stream,
                            const unsigned char *data, size_t length) {
  capture_t *c = context;
  jb_string_t *text = stream == JB_PROCESS_STDOUT ? &c->output : &c->error;
  const size_t limit = 16u * 1024u * 1024u;
  if (length > limit || jb_string_size(text) > limit - length)
    return HAL_EOVERFLOW;
  return jb_string_append(text, (const char *)data, length);
}

static bool whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static jb_json_span_t trim(jb_json_span_t text) {
  while (text.length && whitespace(text.data[0])) {
    ++text.data;
    --text.length;
  }
  while (text.length && whitespace(text.data[text.length - 1]))
    --text.length;
  return text;
}

static void run(jh_hw_t *h, const char *compiler, const jb_vector_t *flags,
                const char *directory, const char *const *extra, size_t count,
                const char *what, jb_string_t *out) {
  if (h->memory.status != HAL_OK)
    return;
  jb_vector_t argv = {0};
  capture_t c = {{0}, {0}};
  (void)jb_string_init(&c.output, jb_heap_allocator());
  (void)jb_string_init(&c.error, jb_heap_allocator());
  jh_node_storage(&h->memory, jb_vector_init(&argv, sizeof(const char *),
                                             jb_heap_allocator()));
  for (size_t i = 0; flags != NULL && i < jb_vector_size(flags); ++i) {
    const char *flag = jb_string_data(jh_list_at(flags, i));
    jh_node_storage(&h->memory, jb_vector_append(&argv, &flag, 1));
  }
  jh_node_storage(&h->memory, jb_vector_append(&argv, extra, count));
  const char *end = NULL;
  jh_node_storage(&h->memory, jb_vector_append(&argv, &end, 1));
  const jb_process_options_t options = {compiler,  jb_vector_const_data(&argv),
                                        directory, NULL,
                                        120000,    capture,
                                        NULL,      &c};
  jb_process_result_t result = {0};
  const hal_status_t status = h->memory.status == HAL_OK
                                  ? jb_process_run(&options, &result)
                                  : h->memory.status;
  if (status == HAL_ENOMEM || status == HAL_EOVERFLOW)
    jh_node_storage(&h->memory, status);
  else if (status != HAL_OK || !result.exited || result.term_signal != 0 ||
           result.exit_code != 0)
    jh_hw_error(h, "JH-HW-GENERATION", (jh_hw_origin_t){compiler, ""}, NULL,
                JH_PARTS(jh_span("the compiler cannot report "), jh_span(what),
                         jh_span(": "),
                         status == HAL_OK
                             ? trim(jh_string_span(&c.error))
                             : jh_span(hal_status_to_string(status))));
  else
    jh_node_storage(&h->memory, jb_string_assign(out, jb_string_data(&c.output),
                                                 jb_string_size(&c.output)));
  (void)jh_hw_phase(h);
  jb_string_destroy(&c.error);
  jb_string_destroy(&c.output);
  jb_vector_destroy(&argv);
}

static jb_json_tree_view_t parse_macros(jh_hw_t *h, jb_json_span_t text) {
  jb_json_tree_t *result = jh_hw_object(h);
  while (h->memory.status == HAL_OK && text.length) {
    const char *end = memchr(text.data, '\n', text.length);
    const size_t length = end != NULL ? (size_t)(end - text.data) : text.length;
    jb_json_span_t line = trim((jb_json_span_t){text.data, length});
    const size_t consumed = length + (end != NULL ? 1u : 0u);
    text.data += consumed;
    text.length -= consumed;
    if (line.length < 8 || memcmp(line.data, "#define", 7) != 0 ||
        !whitespace(line.data[7]))
      continue;
    line = trim((jb_json_span_t){line.data + 7, line.length - 7});
    size_t n = 0;
    while (n < line.length && !whitespace(line.data[n]))
      ++n;
    const jb_json_span_t name = {line.data, n};
    for (size_t i = 0; i < COUNTOF(abi_macros); ++i)
      if (jh_span_equal(name, jh_span(abi_macros[i])))
        jh_hw_set(h, result, abi_macros[i],
                  jh_hw_string(h, trim((jb_json_span_t){line.data + n,
                                                        line.length - n})));
  }
  return jh_hw_root(result);
}

jb_json_tree_view_t jh_hw_compiler_abi(jh_hw_t *h, const char *compiler,
                                       const jb_vector_t *flags,
                                       jb_json_tree_view_t *layout) {
  jb_string_t executable = {0}, source = {0}, output = {0};
  (void)jb_string_init(&executable, jb_heap_allocator());
  (void)jb_string_init(&source, jb_heap_allocator());
  (void)jb_string_init(&output, jb_heap_allocator());
  jb_temp_dir_t *scratch = NULL;
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_process_executable(compiler, h->base, false,
                                             &executable, h->failure);
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_process_scratch(&scratch, h->failure);
  const char *directory = scratch != NULL ? jb_temp_dir_path(scratch) : "";
  const char *probe = jh_hw_path(h, directory, "probe.c");
  /* An empty regular file is portable to Windows GCC and keeps compiler
   * side outputs inside the scratch directory even for the macro pass. */
  bool changed = false;
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jb_file_write_atomic(probe, "", 0, false, &changed));
  const char *const macro_args[] = {"-dM", "-E", "-x", "c", probe};
  run(h, jb_string_data(&executable), flags, directory, macro_args,
      COUNTOF(macro_args), "its macros", &output);
  const jb_json_tree_view_t macros = parse_macros(h, jh_string_span(&output));
  jh_put(&source, &h->memory.status, "#include <stddef.h>\n");
  for (size_t i = 0; i < COUNTOF(probes); ++i) {
    jh_put(&source, &h->memory.status, probes[i].declaration);
    jh_put(&source, &h->memory.status, "\n");
  }
  jh_put(&source, &h->memory.status,
         "void jh_abi_layout_probe(void);\nvoid jh_abi_layout_probe(void) {\n");
  for (size_t i = 0; i < COUNTOF(probes); ++i) {
    jh_put(&source, &h->memory.status,
           "  __asm__ volatile(\".ascii \\\"JH_ABI_LAYOUT ");
    jh_put(&source, &h->memory.status, probes[i].name);
    jh_put(&source, &h->memory.status, " %c0\\\"\" : : \"i\"(");
    jh_put(&source, &h->memory.status, probes[i].expression);
    jh_put(&source, &h->memory.status, "));\n");
  }
  jh_put(&source, &h->memory.status, "}\n");
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_file_write_atomic(
                                    probe, jb_string_data(&source),
                                    jb_string_size(&source), false, &changed));
  const char *const layout_args[] = {"-w", "-fno-lto", "-S", "-o", "-", probe};
  run(h, jb_string_data(&executable), flags, directory, layout_args,
      COUNTOF(layout_args), "its struct layout", &output);
  jb_json_tree_t *values = jh_hw_object(h);
  jb_string_t missing = {0};
  (void)jb_string_init(&missing, jb_heap_allocator());
  for (size_t i = 0; h->memory.status == HAL_OK && i < COUNTOF(probes); ++i) {
    const char *mark =
        jh_hw_join(h, JH_PARTS(jh_span("JH_ABI_LAYOUT "),
                               jh_span(probes[i].name), jh_span(" ")));
    const char *at = strstr(jb_string_data(&output), mark);
    int64_t number = 0;
    if (at != NULL) {
      at += strlen(mark);
      const char *end = at;
      while (jh_ascii_is_digit(*end))
        ++end;
      const jb_json_tree_view_t value =
          end == at ? (jb_json_tree_view_t){0}
                    : jh_hw_root(jh_hw_parse_tree(
                          h, (jb_json_span_t){at, (size_t)(end - at)}));
      if (end != at && jh_hw_i64(h, value, &number) && number >= 0) {
        jh_hw_set(h, values, probes[i].name, jh_hw_integer(h, number));
        continue;
      }
    }
    if (jb_string_size(&missing) != 0)
      jh_put(&missing, &h->memory.status, ", ");
    jh_put(&missing, &h->memory.status, probes[i].name);
  }
  if (jb_string_size(&missing) != 0)
    jh_hw_error(
        h, "JH-HW-GENERATION", (jh_hw_origin_t){compiler, ""}, NULL,
        JH_PARTS(jh_span("the compiler output has no struct layout for "),
                 jh_string_span(&missing)));
  jb_string_destroy(&missing);
  *layout = jh_hw_root(values);
  const hal_status_t cleanup = jb_temp_dir_remove(&scratch);
  jh_node_storage(&h->memory, cleanup);
  jb_string_destroy(&output);
  jb_string_destroy(&source);
  jb_string_destroy(&executable);
  return macros;
}

void jh_hw_abi_checks(jh_hw_t *h, jb_json_tree_view_t abi, jb_string_t *out) {
  jh_put(out, &h->memory.status,
         "/* The compiler ABI this configuration was finalized with. */\n");
  for (size_t i = 0; i < COUNTOF(abi_macros); ++i) {
    const char *name = abi_macros[i];
    const jb_json_tree_view_t value = jh_json_field(abi, name);
    const char *text = value.tree != NULL ? jh_hw_text(h, value) : "";
    jh_put(out, &h->memory.status,
           value.tree != NULL ? "#if !defined(" : "#if defined(");
    jh_put(out, &h->memory.status, name);
    jh_put(out, &h->memory.status, ")");
    if (*text != '\0') {
      jh_put(out, &h->memory.status, " || (");
      jh_put(out, &h->memory.status, name);
      jh_put(out, &h->memory.status, ") != (");
      jh_put(out, &h->memory.status, text);
      jh_put(out, &h->memory.status, ")");
    }
    jh_put(out, &h->memory.status,
           "\n#error \"compiled with another ABI than the configuration (");
    jh_put(out, &h->memory.status, name);
    jh_put(out, &h->memory.status,
           value.tree == NULL ? " undefined"
           : *text            ? "="
                              : " defined");
    jh_put(out, &h->memory.status, text);
    jh_put(out, &h->memory.status,
           "); finalize it with the flags this unit compiles with\"\n#endif\n");
  }
}

void jh_hw_layout_checks(jh_hw_t *h, jb_json_tree_view_t layout,
                         jb_vector_t *lines) {
  jh_node_storage(
      &h->memory,
      jh_list_push(lines, jh_span("/* The struct layout this configuration was "
                                  "finalized with. */")));
  jh_node_storage(&h->memory,
                  jh_list_push(lines, jh_span("#include <stddef.h>")));
  jh_node_storage(
      &h->memory,
      jh_list_push(lines, jh_span("#include <hal/core/hal_status.h>")));
  for (size_t i = 0; i < COUNTOF(probes); ++i)
    jh_node_storage(&h->memory,
                    jh_list_push(lines, jh_span(probes[i].declaration)));
  for (size_t i = 0; i < COUNTOF(probes); ++i) {
    const char *value = jh_hw_repr(h, jh_json_field(layout, probes[i].name));
    jh_node_storage(
        &h->memory,
        jh_list_push_parts(
            lines,
            JH_PARTS(jh_span("JB_STATIC_ASSERT("),
                     jh_span(probes[i].expression), jh_span(" == "),
                     jh_span(value),
                     jh_span("u, \"compiled with another struct layout than "
                             "the configuration ("),
                     jh_span(probes[i].name), jh_span("="), jh_span(value),
                     jh_span("); packing or enum size differ\");"))));
  }
}

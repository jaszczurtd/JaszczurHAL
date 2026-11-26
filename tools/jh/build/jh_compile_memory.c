/* The memory map overview of jh compile: print_memory_map_overview of
 * vscode/runtime/jh_vscode.py over objdump -h of the built ELF
 * (jh_compile_internal.h). C11. */
#include "jh_compile_internal.h"
#include "jh_python_repr.h"
#include <inttypes.h>
#include <string.h>

/* format_size: bytes below 1 KiB, KiB with one decimal below 1 MiB, else MiB
 * with two. */
const char *cc_format_size(jh_compile_ctx_t *c, uint64_t size) {
  char text[64];
  const double kib = (double)size / 1024.0;
  if (size < 1024)
    (void)snprintf(text, sizeof text, "%" PRIu64 " B", size);
  else if (kib < 1024.0)
    (void)snprintf(text, sizeof text, "%.1f KiB", kib);
  else
    (void)snprintf(text, sizeof text, "%.2f MiB", kib / 1024.0);
  return CC_TEXT(c, jh_span(text));
}

bool cc_literal_at(jb_json_span_t text, size_t at, const char *literal) {
  const size_t length = strlen(literal);
  return at <= text.length && text.length - at >= length &&
         memcmp(text.data + at, literal, length) == 0;
}

enum {
  SECTION_ALLOC = 1u,
  SECTION_LOAD = 2u,
  SECTION_CODE = 4u,
  SECTION_READONLY = 8u
};

typedef struct {
  const char *name;
  uint64_t size, vma, lma;
  unsigned flags;
} section_t;

static bool space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

/* \s+ at line[*at]: false without one. */
static bool skip_spaces(jb_json_span_t line, size_t *at) {
  const size_t first = *at;
  while (*at < line.length && space(line.data[*at]))
    ++*at;
  return *at != first;
}

/* [0-9a-fA-F]+ (hex) or \d+ (decimal) at line[*at], as an unsigned value;
 * false without a digit or beyond 64 bits. */
static bool number_at(jb_json_span_t line, size_t *at, bool hex,
                      uint64_t *value) {
  const size_t first = *at;
  const unsigned base = hex ? 16u : 10u;
  *value = 0;
  while (*at < line.length) {
    const int digit = jh_hex_digit(line.data[*at]);
    if (digit < 0 || (unsigned)digit >= base)
      break;
    if (*value > (UINT64_MAX - (uint64_t)digit) / base)
      return false;
    *value = *value * base + (uint64_t)digit;
    ++*at;
  }
  return *at != first;
}

/* SECTION_HEADER_RE of jh-vscode at the start of an objdump -h line:
 * index, name, size, VMA, LMA, file offset and 2**alignment. */
static bool section_header(jh_compile_ctx_t *c, jb_json_span_t line,
                           section_t *section) {
  size_t at = 0;
  uint64_t ignored = 0;
  (void)skip_spaces(line, &at);
  if (!number_at(line, &at, false, &ignored) || !skip_spaces(line, &at))
    return false;
  const size_t name = at;
  while (at < line.length && !space(line.data[at]))
    ++at;
  if (at == name)
    return false;
  const jb_json_span_t text = {line.data + name, at - name};
  uint64_t *fields[] = {&section->size, &section->vma, &section->lma, &ignored};
  for (size_t i = 0; i < COUNTOF(fields); ++i)
    if (!skip_spaces(line, &at) || !number_at(line, &at, true, fields[i]))
      return false;
  if (!skip_spaces(line, &at) || !cc_literal_at(line, at, "2**"))
    return false;
  at += 3;
  if (!number_at(line, &at, false, &ignored))
    return false;
  section->name = CC_TEXT(c, text);
  section->flags = 0;
  return cc_ok(c);
}

/* The flags line after a header: comma separated, each stripped. */
static unsigned section_flags(jb_json_span_t line) {
  static const struct {
    const char *name;
    unsigned flag;
  } known[] = {{"ALLOC", SECTION_ALLOC},
               {"LOAD", SECTION_LOAD},
               {"CODE", SECTION_CODE},
               {"READONLY", SECTION_READONLY}};
  unsigned flags = 0;
  size_t at = 0;
  while (at <= line.length) {
    size_t end = at;
    while (end < line.length && line.data[end] != ',')
      ++end;
    jb_json_span_t part = {line.data + at, end - at};
    (void)jh_python_strip(part, &part);
    for (size_t i = 0; i < COUNTOF(known); ++i)
      if (jh_span_equal(part, jh_span(known[i].name)))
        flags |= known[i].flag;
    at = end + 1;
  }
  return flags;
}

static bool is_flash(uint64_t address) {
  return (address >= 0x08000000u && address < 0x09000000u) ||
         (address >= 0x10000000u && address < 0x11000000u);
}

static bool is_psram(uint64_t address) {
  return address >= 0x11000000u && address < 0x12000000u;
}

static bool is_sram(uint64_t address) {
  return address >= 0x20000000u && address < 0x30000000u;
}

static const char *section_region(const section_t *section) {
  if (strncmp(section->name, ".ccmram", 7) == 0 || is_sram(section->vma))
    return "SRAM";
  if (is_flash(section->vma))
    return "FLASH/XIP";
  if (is_psram(section->vma))
    return "PSRAM/XIP";
  return "OTHER";
}

static bool reserved_ram(const section_t *section) {
  return strstr(section->name, "heap") != NULL ||
         strstr(section->name, "stack") != NULL;
}

static const char *section_note(const section_t *section) {
  const char *name = section->name;
  static const struct {
    const char *name, *note;
  } fixed[] = {{".boot2", "boot block"},
               {".ota", "OTA metadata"},
               {".partition", "partition table"},
               {".flash_end", "flash marker"}};
  for (size_t i = 0; i < COUNTOF(fixed); ++i)
    if (strcmp(name, fixed[i].name) == 0)
      return fixed[i].note;
  if (strcmp(name, ".data") == 0 && is_sram(section->vma) &&
      is_flash(section->lma))
    return "RAM runtime, FLASH init";
  if (strcmp(name, ".bss") == 0 || strcmp(name, ".tbss") == 0)
    return "zeroed RAM";
  if (strcmp(name, ".noinit") == 0)
    return "retained/noinit RAM";
  if (strncmp(name, ".ccmram", 7) == 0)
    return "CPU-only SRAM";
  if (strstr(name, "heap") != NULL)
    return "reserved heap";
  if (strstr(name, "stack") != NULL)
    return "reserved stack";
  if (strcmp(name, ".ram_vector_table") == 0)
    return "runtime vector table";
  if ((section->flags & SECTION_CODE) != 0)
    return "code";
  if ((section->flags & SECTION_READONLY) != 0)
    return "read-only data";
  if ((section->flags & SECTION_LOAD) == 0)
    return "reserved";
  return "data";
}

/* memory_overview_enabled: JH_VSCODE_MEMORY_OVERVIEW, stripped and lowered,
 * is not 0, false, no or off (unset means on). */
static bool overview_enabled(jh_compile_ctx_t *c) {
  const char *value = cc_environment(c, "JH_VSCODE_MEMORY_OVERVIEW");
  if (value == NULL)
    return true;
  jb_json_span_t text = jh_span(value);
  (void)jh_python_strip(text, &text);
  static const char *const off[] = {"0", "false", "no", "off"};
  for (size_t i = 0; i < COUNTOF(off); ++i) {
    bool same = text.length == strlen(off[i]);
    for (size_t k = 0; same && k < text.length; ++k) {
      const char ch = text.data[k];
      same = (jh_ascii_is_upper(ch) ? (char)(ch | 0x20) : ch) == off[i][k];
    }
    if (same)
      return false;
  }
  return true;
}

static void overview_warning(const char *text, const char *detail) {
  (void)fprintf(stderr, "warning: memory map overview skipped; %s%s\n", text,
                detail);
  (void)fflush(stderr);
}

/* objdump -h of the ELF, parsed into the allocated sections with a size and
 * an address. A program that fails is a warning, never a build failure. */
static bool read_sections(jh_compile_ctx_t *c, const char *objdump,
                          const char *elf, jb_vector_t *sections) {
  jb_vector_t arguments = {0};
  jb_string_t out = {0}, err = {0}, text = {0};
  bool result = false;
  if (!esp_list_init(&c->e, &arguments) ||
      !cc_storage(c, jb_string_init(&out, jb_heap_allocator())) ||
      !cc_storage(c, jb_string_init(&err, jb_heap_allocator())) ||
      !cc_storage(c, jb_string_init(&text, jb_heap_allocator())) ||
      !esp_push(&c->e, &arguments, jh_span("-h")) ||
      !esp_push(&c->e, &arguments, jh_span(elf)))
    goto done;
  const esp_process_t process = {
      objdump, &arguments, NULL, NULL, ESP_RUN_CAPTURE, false, true,
      0,       &out,       &err, 0};
  int code = -1;
  const hal_status_t status = esp_process(&c->e, &process, &code);
  if (!cc_ok(c))
    goto done;
  if (status != HAL_OK) {
    overview_warning(
        CC_TEXT(c, jh_span("failed to run "), jh_span(objdump), jh_span(": ")),
        hal_status_to_string(status));
    goto done;
  }
  if (code != 0) {
    const jb_string_t *source = jb_string_size(&err) != 0 ? &err : &out;
    if (esp_decode_text(&c->e, jh_string_span(source), &text)) {
      const jb_json_span_t message = esp_strip(jh_string_span(&text));
      if (message.length != 0)
        overview_warning("objdump failed: ", CC_TEXT(c, message));
    }
    goto done;
  }
  if (!esp_decode_text(&c->e, jh_string_span(&out), &text))
    goto done;
  const char *data = jb_string_data(&text);
  const size_t size = jb_string_size(&text);
  section_t current = {0};
  bool open = false;
  for (size_t at = 0; at < size && cc_ok(c);) {
    const char *newline = memchr(data + at, '\n', size - at);
    const size_t end = newline != NULL ? (size_t)(newline - data) : size;
    const jb_json_span_t line = {data + at, end - at};
    at = end + 1;
    section_t header = {0};
    if (section_header(c, line, &header)) {
      if (open)
        (void)cc_storage(c, jb_vector_append(sections, &current, 1));
      current = header;
      open = true;
      continue;
    }
    jb_json_span_t stripped = line;
    (void)jh_python_strip(line, &stripped);
    if (open && stripped.length != 0) {
      current.flags = section_flags(stripped);
      (void)cc_storage(c, jb_vector_append(sections, &current, 1));
      open = false;
    }
  }
  if (open)
    (void)cc_storage(c, jb_vector_append(sections, &current, 1));
  /* Keep the sections that occupy memory: a size, an address and ALLOC. */
  size_t kept = 0;
  for (size_t i = 0; i < jb_vector_size(sections); ++i) {
    const section_t *item = jb_vector_const_at(sections, i);
    if (item->size > 0 && item->vma != 0 && (item->flags & SECTION_ALLOC) != 0)
      *(section_t *)jb_vector_at(sections, kept++) = *item;
  }
  (void)cc_storage(c, jb_vector_resize(sections, kept));
  result = cc_ok(c);
done:
  jb_string_destroy(&text);
  jb_string_destroy(&err);
  jb_string_destroy(&out);
  jh_list_destroy(&arguments);
  return result;
}

/* The code points of UTF-8 text: its bytes other than continuations. */
static size_t code_points(const char *text) {
  size_t count = 0;
  for (; *text != '\0'; ++text)
    count += ((unsigned char)*text & 0xC0u) != 0x80u;
  return count;
}

static const char *format_range(jh_compile_ctx_t *c, uint64_t start,
                                uint64_t size) {
  char text[48];
  (void)snprintf(text, sizeof text, "0x%08" PRIx64 "-0x%08" PRIx64, start,
                 start + size - 1);
  return CC_TEXT(c, jh_span(text));
}

static bool print_overview(jh_compile_ctx_t *c, const char *elf,
                           const jb_vector_t *sections) {
  static const char *const regions[] = {"FLASH/XIP", "SRAM", "PSRAM/XIP",
                                        "OTHER"};
  FILE *log = c->e.log;
  int failed = fprintf(log, "\nMemory map overview: %s\n", elf) < 0;
  uint64_t flash = 0, sram = 0, reserved = 0;
  for (size_t i = 0; i < jb_vector_size(sections); ++i) {
    const section_t *item = jb_vector_const_at(sections, i);
    if (is_flash(item->lma) && (item->flags & SECTION_LOAD) != 0)
      flash += item->size;
    if (strcmp(section_region(item), "SRAM") == 0)
      *(reserved_ram(item) ? &reserved : &sram) += item->size;
  }
  for (size_t r = 0; r < COUNTOF(regions) && cc_ok(c); ++r) {
    uint64_t total = 0;
    size_t count = 0;
    for (size_t i = 0; i < jb_vector_size(sections); ++i) {
      const section_t *item = jb_vector_const_at(sections, i);
      if (strcmp(section_region(item), regions[r]) == 0) {
        total += item->size;
        ++count;
      }
    }
    if (count == 0)
      continue;
    failed |= fprintf(log,
                      "%s (%s by VMA):\n  section              VMA range    "
                      "          LMA          size       note\n",
                      regions[r], cc_format_size(c, total)) < 0;
    for (size_t i = 0; i < jb_vector_size(sections) && cc_ok(c); ++i) {
      const section_t *item = jb_vector_const_at(sections, i);
      if (strcmp(section_region(item), regions[r]) != 0)
        continue;
      char lma[32];
      if ((item->flags & SECTION_LOAD) == 0)
        (void)snprintf(lma, sizeof lma, "no-load");
      else if (item->lma == item->vma)
        (void)snprintf(lma, sizeof lma, "same");
      else
        (void)snprintf(lma, sizeof lma, "0x%08" PRIx64, item->lma);
      /* f"{name:<20}" pads by characters. */
      const size_t width = code_points(item->name);
      failed |= fprintf(log, "  %s%*s %-22s %-12s %9s  %s\n", item->name,
                        width < 20 ? (int)(20 - width) : 0, "",
                        format_range(c, item->vma, item->size), lma,
                        cc_format_size(c, item->size), section_note(item)) < 0;
    }
  }
  if (cc_ok(c))
    failed |= fprintf(log,
                      "Totals: FLASH load sections %s, SRAM static %s, SRAM "
                      "reserved heap/stack %s\n",
                      cc_format_size(c, flash), cc_format_size(c, sram),
                      cc_format_size(c, reserved)) < 0;
  if (cc_ok(c) && (failed || fflush(log) != 0))
    (void)jh_fail_text(c->e.failure, HAL_EIO, 3, "cannot write the log");
  return cc_ok(c);
}

bool cc_memory_overview(jh_compile_ctx_t *c, const char *tree,
                        const char *objdump) {
  if (!overview_enabled(c))
    return cc_ok(c);
  const char *elf = esp_join(&c->e, tree, "firmware.elf");
  if (!cc_ok(c) || !esp_is_file(elf))
    return cc_ok(c);
  if (objdump == NULL) {
    overview_warning("target objdump not found", "");
    return cc_ok(c);
  }
  jb_vector_t sections = {0};
  if (cc_storage(c, jb_vector_init(&sections, sizeof(section_t),
                                   jb_heap_allocator())) &&
      read_sections(c, objdump, elf, &sections) &&
      jb_vector_size(&sections) != 0)
    (void)print_overview(c, elf, &sections);
  jb_vector_destroy(&sections);
  return cc_ok(c);
}

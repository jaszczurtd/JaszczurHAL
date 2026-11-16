#include "hal/core/jh_endian.h"
#include "hal/security/jh_sha256.h"
#include "jh_firmware_internal.h"
#include "jh_process_support.h"
#include <jb/filesystem.h>
#include <stdlib.h>
#include <string.h>

static bool require(jh_firmware_plan_t *p, hal_status_t status,
                    const char *message) {
  if (p->failure->status == HAL_OK)
    (void)jh_require(p->failure, status, 2, message);
  return p->failure->status == HAL_OK;
}

const char *jh_firmware_text(jh_firmware_plan_t *p, jb_json_span_t value) {
  if (p->failure->status != HAL_OK)
    return "";
  if (!require(p,
               value.length <= 32768 &&
                       value.length <=
                           JH_FIRMWARE_METADATA_BYTES - p->string_bytes
                   ? HAL_OK
                   : HAL_EOVERFLOW,
               "build metadata text limit exceeded"))
    return "";
  if (!require(p, jh_list_push(&p->strings, value),
               "cannot store build metadata"))
    return "";
  p->string_bytes += value.length;
  return jb_string_data(
      jh_list_at(&p->strings, jb_vector_size(&p->strings) - 1));
}

const char *jh_firmware_path(jh_firmware_plan_t *p, const char *base,
                             const char *name) {
  if (p->failure->status != HAL_OK)
    return "";
  jb_string_t path = {0};
  hal_status_t status = jb_string_init(&path, jb_heap_allocator());
  if (status == HAL_OK)
    status = jh_absolute_path(name, base, &path, p->failure);
  const char *out = require(p, status, "cannot resolve build path")
                        ? jh_firmware_text(p, jh_string_span(&path))
                        : "";
  jb_string_destroy(&path);
  return out;
}

void jh_firmware_sha256(const void *data, size_t length, char out[65]) {
  jh_sha256_context_t context;
  uint8_t digest[JH_SHA256_DIGEST_BYTES];
  static const char hex[] = "0123456789abcdef";
  jh_sha256_init(&context);
  jh_sha256_update(&context, data, length);
  jh_sha256_final(&context, digest);
  for (size_t i = 0; i < COUNTOF(digest); ++i) {
    out[2 * i] = hex[digest[i] >> 4];
    out[2 * i + 1] = hex[digest[i] & 15];
  }
  out[64] = '\0';
}

static bool read_file(jh_firmware_plan_t *p, const char *path,
                      jb_file_data_t *out) {
  if (p->failure->status != HAL_OK)
    return false;
  const hal_status_t status =
      jb_file_read(path, JH_FIRMWARE_METADATA_BYTES, out);
  if (status != HAL_OK)
    (void)jh_fail(
        p->failure, status, 3,
        JH_PARTS(jh_span(path), jh_span(": cannot read build metadata")));
  return status == HAL_OK;
}

static bool exists(jh_firmware_plan_t *p, const char *path) {
  jb_fs_info_t info;
  const hal_status_t status = jb_fs_status(path, true, &info);
  if (status == HAL_ENOENT)
    return false;
  return require(p, status, "cannot inspect build file") &&
         info.kind == JB_FS_REGULAR;
}

/* Iterate bounded, borrowed lines, without assuming a terminating NUL. */
static bool next_line(jb_json_span_t *input, jb_json_span_t *line) {
  if (input->length == 0)
    return false;
  const char *end = memchr(input->data, '\n', input->length);
  const size_t length = end ? (size_t)(end - input->data) : input->length;
  *line = (jb_json_span_t){input->data, length};
  const size_t consumed = length + (end != NULL ? 1u : 0u);
  input->data += consumed;
  input->length -= consumed;
  if (line->length && line->data[line->length - 1] == '\r')
    --line->length;
  return true;
}

static const char *cache_value(jh_firmware_plan_t *p,
                               const jb_file_data_t *cache, const char *key) {
  jb_json_span_t input = {(const char *)cache->data, cache->length}, line;
  const size_t n = strlen(key);
  const char *value = "";
  bool found = false;
  while (next_line(&input, &line)) {
    if (line.length <= n || memcmp(line.data, key, n) != 0 ||
        line.data[n] != ':')
      continue;
    const char *eq = memchr(line.data + n, '=', line.length - n);
    if (!require(p, eq != NULL && !found ? HAL_OK : HAL_ECONFIG,
                 "duplicate or malformed CMake cache entry"))
      break;
    found = true;
    const jb_json_span_t text = {eq + 1,
                                 line.length - (size_t)(eq + 1 - line.data)};
    if (!require(p,
                 memchr(text.data, '\0', text.length) == NULL ? HAL_OK
                                                              : HAL_ECONFIG,
                 "NUL in CMake cache entry"))
      break;
    value = jh_firmware_text(p, text);
  }
  return value;
}

static bool unsigned_value(jb_json_span_t text, uint64_t *out) {
  unsigned base = 10;
  size_t i = 0;
  if (text.length > 2 && text.data[0] == '0' &&
      (text.data[1] == 'x' || text.data[1] == 'X')) {
    base = 16;
    i = 2;
  }
  if (i == text.length)
    return false;
  uint64_t value = 0;
  for (; i < text.length; ++i) {
    const int digit = jh_hex_digit(text.data[i]);
    if (digit < 0 || (unsigned)digit >= base ||
        value > (UINT64_MAX - (unsigned)digit) / base)
      return false;
    value = value * base + (unsigned)digit;
  }
  *out = value;
  return true;
}

static bool add_region(jh_firmware_plan_t *p, uint64_t start, uint64_t size) {
  if (!require(p,
               size && size <= JH_FIRMWARE_MAX_BYTES && start <= UINT32_MAX &&
                       size <= UINT64_C(0x100000000) - start &&
                       p->region_count < COUNTOF(p->regions)
                   ? HAL_OK
                   : HAL_ECONFIG,
               "invalid flash region in build map"))
    return false;
  for (size_t i = 0; i < p->region_count; ++i)
    if (!require(p,
                 start >= p->regions[i].end ||
                         start + size <= p->regions[i].begin
                     ? HAL_OK
                     : HAL_ECONFIG,
                 "overlapping flash regions in build map"))
      return false;
  p->regions[p->region_count++] = (jh_flash_region_t){start, start + size};
  return true;
}

static jb_json_span_t word(jb_json_span_t *line) {
  while (line->length && (line->data[0] == ' ' || line->data[0] == '\t')) {
    ++line->data;
    --line->length;
  }
  size_t n = 0;
  while (n < line->length && line->data[n] != ' ' && line->data[n] != '\t')
    ++n;
  const jb_json_span_t result = {line->data, n};
  line->data += n;
  line->length -= n;
  return result;
}

static void native_map(jh_firmware_plan_t *p, const char *directory) {
  jb_file_data_t map = {0};
  if (!read_file(p, jh_firmware_path(p, directory, "firmware.map"), &map))
    return;
  jb_json_span_t input = {(const char *)map.data, map.length}, line;
  bool memory = false, flash = false, symbols = false;
  bool have_start = false, have_size = false;
  uint64_t device_start = 0, device_size = 0;
  while (next_line(&input, &line)) {
    if (jh_span_equal(line, jh_span("Memory Configuration"))) {
      memory = true;
      continue;
    }
    if (jh_span_equal(line, jh_span("Linker script and memory map"))) {
      memory = false;
      symbols = true;
      continue;
    }
    if (symbols) {
      uint64_t value = 0;
      const jb_json_span_t number = word(&line), name = word(&line);
      if (jh_span_equal(name, jh_span("__jh_flash_device_start")) ||
          jh_span_equal(name, jh_span("__jh_flash_device_size"))) {
        if (!require(p, unsigned_value(number, &value) ? HAL_OK : HAL_ECONFIG,
                     "invalid physical flash symbol"))
          break;
        if (jh_span_equal(name, jh_span("__jh_flash_device_start"))) {
          (void)require(p, !have_start ? HAL_OK : HAL_ECONFIG,
                        "duplicate flash start symbol");
          device_start = value;
          have_start = true;
        } else {
          (void)require(p, !have_size ? HAL_OK : HAL_ECONFIG,
                        "duplicate flash size symbol");
          device_size = value;
          have_size = true;
        }
      }
    }
    if (!memory)
      continue;
    const jb_json_span_t name = word(&line);
    const bool primary = jh_span_equal(name, jh_span("FLASH"));
    if (!primary && !jh_span_equal(name, jh_span("HAL_LITTLEFS_FLASH")) &&
        !jh_span_equal(name, jh_span("HAL_EEPROM_FLASH")))
      continue;
    uint64_t start = 0, size = 0;
    if (!require(p,
                 unsigned_value(word(&line), &start) &&
                         unsigned_value(word(&line), &size)
                     ? HAL_OK
                     : HAL_ECONFIG,
                 "invalid memory map address or length"))
      break;
    if (!size && !primary)
      continue;
    if (!add_region(p, start, size))
      break;
    if (primary) {
      flash = true;
      p->address = (uint32_t)start;
      p->size = (uint32_t)size;
    }
  }
  (void)require(p, flash ? HAL_OK : HAL_ECONFIG,
                "build map has no FLASH region");
  if (strcmp(p->target, "stm32g474") == 0) {
    device_start = p->address;
    uint64_t end = device_start;
    for (size_t pass = 0; pass < p->region_count; ++pass)
      for (size_t i = 0; i < p->region_count; ++i)
        if (p->regions[i].begin <= end && p->regions[i].end > end)
          end = p->regions[i].end;
    device_size = end - device_start;
  } else {
    (void)require(
        p, have_start && have_size ? HAL_OK : HAL_ECONFIG,
        "RP map lacks physical flash symbols; rebuild with the current HAL");
  }
  for (size_t i = 0; i < p->region_count; ++i)
    (void)require(p,
                  device_size <= JH_FIRMWARE_MAX_BYTES &&
                          p->regions[i].begin >= device_start &&
                          p->regions[i].end - device_start <= device_size
                      ? HAL_OK
                      : HAL_ECONFIG,
                  "mapped flash leaves the physical device");
  p->region_count = 0;
  if (p->failure->status == HAL_OK &&
      add_region(p, device_start, device_size)) {
    p->address = (uint32_t)device_start;
    p->size = (uint32_t)device_size;
  }
  jb_file_release(&map);
}

static jb_json_tree_t *read_json(jh_firmware_plan_t *p, const char *path,
                                 jb_json_tree_view_t *root) {
  jb_json_tree_t *tree = NULL;
  if (p->failure->status == HAL_OK &&
      jh_json_read_value(p->location, path, JH_FIRMWARE_METADATA_BYTES, &tree,
                         p->failure) == HAL_OK)
    (void)require(p, jb_json_tree_root(tree, root), "invalid build JSON");
  return tree;
}

static const char *json_text(jh_firmware_plan_t *p, jb_json_tree_view_t object,
                             const char *key) {
  jb_json_span_t text = {0};
  const jb_json_tree_view_t value = jh_json_field(object, key);
  if (!require(p, jb_json_tree_text(value, &text), "missing build JSON text") ||
      !require(p,
               text.length && memchr(text.data, '\0', text.length) == NULL
                   ? HAL_OK
                   : HAL_ECONFIG,
               "invalid build JSON text"))
    return "";
  return jh_firmware_text(p, text);
}

static bool same_path(jh_firmware_plan_t *p, const char *a, const char *b) {
  bool same = false;
  const hal_status_t status = jb_fs_equivalent(a, b, &same);
  if (status == HAL_ENOENT)
    return false;
  return require(p, status, "cannot compare project paths") && same;
}

/* ESP-IDF's generated project descriptor binds external builds to source
 * content. Verify every recorded source and header before selecting its map. */
static void verify_source(jh_firmware_plan_t *p, const char *path,
                          const char *expected) {
  jb_file_data_t data = {0};
  if (read_file(p, jh_firmware_path(p, p->options->project, path), &data)) {
    char digest[65];
    jh_firmware_sha256(data.data, data.length, digest);
    (void)require(p, strcmp(digest, expected) == 0 ? HAL_OK : HAL_ECONFIG,
                  "ESP build does not match project sources; rebuild or select "
                  "another build");
  }
  jb_file_release(&data);
}

static void esp_map(jh_firmware_plan_t *p, jb_json_tree_view_t manifest) {
  const jb_json_tree_view_t artifacts = jh_json_field(manifest, "artifacts");
  jb_json_tree_view_t project = {0}, flasher = {0};
  jb_json_tree_t *project_tree =
      read_json(p,
                jh_firmware_path(p, p->directory,
                                 json_text(p, artifacts, "projectConfig")),
                &project);
  jb_json_tree_t *flasher_tree = read_json(
      p,
      jh_firmware_path(p, p->directory, json_text(p, artifacts, "flasherArgs")),
      &flasher);
  const jb_json_tree_view_t header =
      jh_json_field(project, "projectConfigHeader");
  verify_source(p, json_text(p, header, "path"),
                json_text(p, header, "sha256"));
  const jb_json_tree_view_t sources =
      jh_json_field(project, "projectSourceSha256");
  size_t count = 0;
  (void)require(p, jb_json_tree_count(sources, &count),
                "missing ESP source hashes");
  for (size_t i = 0; i < count && p->failure->status == HAL_OK; ++i) {
    jb_json_tree_view_t member = {0};
    jb_json_span_t key = {0}, digest = {0};
    (void)require(p, jb_json_tree_at(sources, i, &member),
                  "invalid source hash");
    (void)require(p, jb_json_tree_key(member, &key), "invalid source path");
    (void)require(p, jb_json_tree_text(member, &digest), "invalid source hash");
    if (!require(p,
                 key.length && !memchr(key.data, '\0', key.length) &&
                         digest.length == 64 &&
                         !memchr(digest.data, '\0', digest.length)
                     ? HAL_OK
                     : HAL_ECONFIG,
                 "invalid source hash entry"))
      break;
    verify_source(p, jh_firmware_text(p, key), jh_firmware_text(p, digest));
  }
  uint64_t app_offset = 0;
  (void)require(p,
                unsigned_value(jh_span(json_text(
                                   p, jh_json_field(flasher, "app"), "offset")),
                               &app_offset)
                    ? HAL_OK
                    : HAL_ECONFIG,
                "invalid ESP app offset");
  const jb_json_tree_view_t table =
      jh_json_field(jh_json_field(manifest, "configuration"), "partitionTable");
  jb_file_data_t bytes = {0};
  bool app_found = false;
  if (read_file(p,
                jh_firmware_path(p, p->directory, json_text(p, table, "path")),
                &bytes)) {
    char digest[65];
    jh_firmware_sha256(bytes.data, bytes.length, digest);
    (void)require(p,
                  strcmp(digest, json_text(p, table, "sha256")) == 0
                      ? HAL_OK
                      : HAL_ECONFIG,
                  "partition table hash differs from build manifest");
    for (size_t i = 0; i + 32 <= bytes.length && p->failure->status == HAL_OK;
         i += 32) {
      const uint8_t *entry = bytes.data + i;
      const uint16_t magic = jh_load_le16(entry);
      if (magic == 0xffff || magic == 0xebeb)
        break;
      if (!require(p, magic == 0x50aa ? HAL_OK : HAL_ECONFIG,
                   "invalid ESP partition entry"))
        break;
      const uint32_t start = jh_load_le32(entry + 4),
                     size = jh_load_le32(entry + 8);
      if (!add_region(p, start, size))
        break;
      if (entry[2] == 0 && start == app_offset) {
        p->address = start;
        p->size = size;
        app_found = true;
      }
    }
  }
  (void)require(p, app_found ? HAL_OK : HAL_ECONFIG,
                "ESP app is absent from partition map");
  const char *capacity =
      json_text(p, jh_json_field(flasher, "flash_settings"), "flash_size");
  const size_t capacity_length = strlen(capacity);
  uint64_t megabytes = 0;
  if (require(p,
              capacity_length > 2 &&
                      strcmp(capacity + capacity_length - 2, "MB") == 0 &&
                      unsigned_value(
                          (jb_json_span_t){capacity, capacity_length - 2},
                          &megabytes) &&
                      megabytes > 0 && megabytes <= 64
                  ? HAL_OK
                  : HAL_ECONFIG,
              "ESP flash size must be explicit in the completed build")) {
    const uint64_t physical_size = megabytes * 1024 * 1024;
    for (size_t i = 0; i < p->region_count; ++i)
      (void)require(p,
                    p->regions[i].end <= physical_size ? HAL_OK : HAL_ECONFIG,
                    "ESP partition leaves the physical flash");
    p->region_count = 0;
    if (add_region(p, 0, physical_size)) {
      p->address = 0;
      p->size = (uint32_t)physical_size;
    }
  }
  jb_file_release(&bytes);
  jb_json_tree_destroy(project_tree);
  jb_json_tree_destroy(flasher_tree);
}

static bool select_build(jh_firmware_plan_t *p, const char *directory,
                         bool explicit_build) {
  const char *cache_path = jh_firmware_path(p, directory, "CMakeCache.txt");
  if (!exists(p, cache_path))
    return false;
  jb_file_data_t cache = {0};
  if (!read_file(p, cache_path, &cache))
    return true;
  const char *manifest_path =
      jh_firmware_path(p, directory, "jh_esp_idf_artifacts.json");
  jb_json_tree_view_t manifest = {0};
  jb_json_tree_t *manifest_tree = NULL;
  const bool esp = exists(p, manifest_path);
  const char *assembly = "", *target = "";
  bool matches = false;
  if (esp) {
    manifest_tree = read_json(p, manifest_path, &manifest);
    assembly = json_text(p, manifest, "assembly");
    if (p->failure->status == HAL_OK &&
        strcmp(assembly, p->options->assembly) != 0) {
      if (explicit_build)
        (void)require(
            p, HAL_ECONFIG,
            "build does not match --project, --assembly and --variant");
      jb_json_tree_destroy(manifest_tree);
      jb_file_release(&cache);
      return true;
    }
    target = json_text(p, manifest, "target");
    jb_json_tree_view_t hardware = {0};
    const jb_json_tree_view_t artifacts = jh_json_field(manifest, "artifacts");
    jb_json_tree_t *hardware_tree = read_json(
        p,
        jh_firmware_path(p, directory,
                         json_text(p, artifacts, "resolvedBoardConfig")),
        &hardware);
    const jb_json_tree_view_t variant_value =
        jh_json_field(jh_json_field(hardware, "software"), "variant");
    jb_json_span_t variant = {"", 0};
    if (jh_json_type(variant_value) != JB_JSON_NULL)
      (void)require(p, jb_json_tree_text(variant_value, &variant),
                    "invalid ESP build variant");
    matches =
        strcmp(assembly, p->options->assembly) == 0 &&
        jh_span_equal(variant,
                      jh_span(p->options->variant ? p->options->variant : ""));
    jb_json_tree_destroy(hardware_tree);
  } else {
    assembly = cache_value(p, &cache, "JH_ASSEMBLY");
    target = cache_value(p, &cache, "JH_TARGET");
    const char *project = cache_value(p, &cache, "JH_PROJECT_DIR");
    const char *variant = cache_value(p, &cache, "JH_VARIANT");
    if (project[0] && (!assembly[0] || !target[0])) {
      const char *hardware_path = jh_firmware_path(
          p, directory, "generated/hardware/jh_hardware_config.json");
      if (exists(p, hardware_path)) {
        jb_json_tree_view_t hardware = {0};
        jb_json_tree_t *tree = read_json(p, hardware_path, &hardware);
        const jb_json_tree_view_t selection =
            jh_json_field(hardware, "selection");
        if (!assembly[0])
          assembly = json_text(p, selection, "assembly");
        if (!target[0])
          target = json_text(p, selection, "target");
        jb_json_tree_destroy(tree);
      }
    }
    matches =
        project[0] && strcmp(assembly, p->options->assembly) == 0 &&
        strcmp(variant, p->options->variant ? p->options->variant : "") == 0 &&
        same_path(p, project, p->options->project);
  }
  if (matches &&
      require(p, p->directory == NULL ? HAL_OK : HAL_ECONFIG,
              "multiple matching builds; select one with --build-dir")) {
    p->directory = directory;
    if (p->locate_only) {
      /* Discovery loads the metadata once every candidate is known. */
    } else if (esp && strcmp(target, "esp32s3") == 0) {
      p->target = "esp32s3";
      p->tool = cache_value(p, &cache, "PYTHON");
      esp_map(p, manifest);
    } else if (!esp && (strcmp(target, "rp2040") == 0 ||
                        strcmp(target, "rp2350-arm") == 0 ||
                        strcmp(target, "rp2350-riscv") == 0 ||
                        strcmp(target, "stm32g474") == 0)) {
      p->target = strcmp(target, "rp2040") == 0         ? "rp2040"
                  : strcmp(target, "rp2350-arm") == 0   ? "rp2350-arm"
                  : strcmp(target, "rp2350-riscv") == 0 ? "rp2350-riscv"
                                                        : "stm32g474";
      const char *artifacts = cache_value(p, &cache, "JH_ARTIFACT_DIR");
      if (!artifacts[0])
        artifacts = jh_firmware_path(p, p->options->project, ".build");
      native_map(p, artifacts);
      p->tool = cache_value(p, &cache,
                            strcmp(target, "stm32g474") == 0
                                ? "OPENOCD_BIN"
                                : "JH_PICOTOOL_EXECUTABLE");
      p->interface_file = cache_value(p, &cache, "OPENOCD_INTERFACE");
      p->target_file = cache_value(p, &cache, "OPENOCD_TARGET");
    } else {
      (void)require(p, HAL_EUNSUPPORTED,
                    "firmware read does not support this build target");
    }
  } else if (!matches && explicit_build) {
    (void)require(p, HAL_ECONFIG,
                  "build does not match --project, --assembly and --variant");
  }
  jb_json_tree_destroy(manifest_tree);
  jb_file_release(&cache);
  return true;
}

typedef struct {
  jh_firmware_plan_t *plan;
  const char *directory;
  size_t depth, *visited;
} search_t;
static hal_status_t collect_directory(void *user, const char *name,
                                      const jb_fs_info_t *info) {
  return info->kind == JB_FS_DIRECTORY ? jh_list_push(user, jh_span(name))
                                       : HAL_OK;
}
static hal_status_t search_builds(search_t *search) {
  jh_firmware_plan_t *p = search->plan;
  if (!require(p,
               ++*search->visited <= 512 && search->depth <= 8 ? HAL_OK
                                                               : HAL_EOVERFLOW,
               "build discovery limit reached; select --build-dir"))
    return p->failure->status;
  if (select_build(p, search->directory, false)) {
    /* Variant builds live beside CMake's generated files. */
    const char *variants = jh_firmware_path(p, search->directory, "variants");
    jb_fs_info_t info;
    if (jb_fs_status(variants, false, &info) == HAL_OK &&
        info.kind == JB_FS_DIRECTORY) {
      search_t child = {p, variants, search->depth + 1, search->visited};
      return search_builds(&child);
    }
    return p->failure->status;
  }
  /* Name order keeps discovery and its diagnostics the same on every file
   * system; readdir order differs between machines. */
  jb_vector_t names = {0};
  hal_status_t status = jh_list_init(&names);
  if (status == HAL_OK)
    status = jb_fs_list(search->directory, collect_directory, &names);
  if (status != HAL_ENOENT)
    (void)require(p, status, "cannot search builds; select --build-dir");
  jh_list_sort(&names);
  for (size_t i = 0; p->failure->status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    search_t child = {p,
                      jh_firmware_path(p, search->directory,
                                       jb_string_data(jh_list_at(&names, i))),
                      search->depth + 1, search->visited};
    (void)search_builds(&child);
  }
  jh_list_destroy(&names);
  return p->failure->status;
}

static void find_tool(jh_firmware_plan_t *p) {
  if (!p->tool || !*p->tool || p->failure->status != HAL_OK)
    return;
  jb_string_t executable = {0};
  (void)jb_string_init(&executable, jb_heap_allocator());
  if (jh_process_executable(p->tool, p->options->project, true, &executable,
                            p->failure) == HAL_OK)
    p->tool = jh_firmware_text(p, jh_string_span(&executable));
  jb_string_destroy(&executable);
}

hal_status_t jh_firmware_prepare(jh_firmware_plan_t *p) {
  const jh_firmware_options_t *o = p->options;
  if (o->build_directory) {
    if (!select_build(p, o->build_directory, true))
      (void)require(p, HAL_ENOENT, "--build-dir has no CMakeCache.txt");
  } else {
    size_t visited = 0;
    search_t search = {p, jh_firmware_path(p, o->project, ".build"), 0,
                       &visited};
    /* Find every match before loading one: a second matching build must be
     * reported whichever candidate comes first or has broken metadata. */
    p->locate_only = true;
    (void)search_builds(&search);
    p->locate_only = false;
    const char *directory = p->directory;
    p->directory = NULL;
    if (directory && p->failure->status == HAL_OK)
      (void)select_build(p, directory, false);
  }
  if (!require(p, p->directory != NULL ? HAL_OK : HAL_ENOENT,
               "no matching completed build; build the assembly or select "
               "--build-dir"))
    return p->failure->status;
  if (o->address || o->size) {
    uint64_t address = 0, size = 0;
    if (!require(p,
                 o->address && o->size &&
                         unsigned_value(jh_span(o->address), &address) &&
                         unsigned_value(jh_span(o->size), &size) && size &&
                         size <= JH_FIRMWARE_MAX_BYTES &&
                         address <= UINT32_MAX &&
                         size <= UINT64_C(0x100000000) - address
                     ? HAL_OK
                     : HAL_EINVAL,
                 "--address and --size require a nonempty decimal/0x range, at "
                 "most 64 MiB"))
      return p->failure->status;
    uint64_t end = address;
    /* Permit adjacent mapped regions but never holes or RAM. */
    for (size_t pass = 0; pass < p->region_count; ++pass)
      for (size_t i = 0; i < p->region_count; ++i)
        if (p->regions[i].begin <= end && p->regions[i].end > end)
          end = p->regions[i].end;
    if (!require(p, address + size <= end ? HAL_OK : HAL_EINVAL,
                 "requested range is outside the build's flash map"))
      return p->failure->status;
    p->address = (uint32_t)address;
    p->size = (uint32_t)size;
  }
  if (o->tool)
    p->tool = o->tool;
  find_tool(p);
  bool absolute = false;
  if (!require(p, p->tool && *p->tool ? HAL_OK : HAL_ENOENT,
               "build has no managed tool; set --tool") ||
      !require(p, jb_fs_path_is_absolute(p->tool, &absolute),
               "invalid tool path") ||
      !require(p, absolute ? HAL_OK : HAL_EINVAL,
               "tool must be absolute; set --tool to its executable path"))
    return p->failure->status;
  return p->failure->status;
}

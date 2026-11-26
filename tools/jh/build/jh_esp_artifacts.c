/* Toolchain provenance (jh_esp_idf_toolchain.json) and the artifact
 * validation and manifest (jh_esp_idf_artifacts.json) of an ESP-IDF build,
 * as scripts/build_esp_idf.py checks and writes them. The CMakeCache.txt
 * entries come from the reader `jh firmware read` uses
 * (jh_build_metadata.h). C11. */
#include "jh_build_metadata.h"
#include "jh_esp_internal.h"
#include "jh_json_check.h"
#include "jh_json_digest.h"
#include "jh_json_file.h"
#include <stdlib.h>
#include <string.h>

#define SNAPSHOT "security/esp_idf_tools.json"
#define TOOLCHAIN_JSON "jh_esp_idf_toolchain.json"
#define MANIFEST_JSON "jh_esp_idf_artifacts.json"

static const char *const partition_profiles[] = {
    "CONFIG_PARTITION_TABLE_SINGLE_APP",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE",
    "CONFIG_PARTITION_TABLE_TWO_OTA",
    "CONFIG_PARTITION_TABLE_TWO_OTA_LARGE",
    "CONFIG_PARTITION_TABLE_CUSTOM",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_ENCRYPTED_NVS",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE_ENC_NVS",
    "CONFIG_PARTITION_TABLE_TWO_OTA_ENCRYPTED_NVS",
    "CONFIG_PARTITION_TABLE_SINGLE_APP_TEE",
    "CONFIG_PARTITION_TABLE_TWO_OTA_TEE",
};

/* One tool list of the supply-chain snapshot. */
typedef struct {
  jb_vector_t names, versions, licenses, upstreams, sources;
} tools_t;

/* load_supply_chain_contract */
typedef struct {
  jb_json_tree_t *tree;
  jb_json_tree_view_t snapshot;
  char sha256[65];
  const char *tools_digest;
  jb_vector_t targets;
  tools_t binary, python;
  int64_t schema;
} contract_t;

static void tools_init(tools_t *tools) {
  jb_vector_t *lists[] = {&tools->names, &tools->versions, &tools->licenses,
                          &tools->upstreams, &tools->sources};
  for (size_t i = 0; i < COUNTOF(lists); ++i)
    (void)jh_list_init(lists[i]);
}

static void tools_destroy(tools_t *tools) {
  jb_vector_t *lists[] = {&tools->names, &tools->versions, &tools->licenses,
                          &tools->upstreams, &tools->sources};
  for (size_t i = 0; i < COUNTOF(lists); ++i)
    jh_list_destroy(lists[i]);
}

static void contract_init(contract_t *contract) {
  memset(contract, 0, sizeof *contract);
  (void)jh_list_init(&contract->targets);
  tools_init(&contract->binary);
  tools_init(&contract->python);
}

static void contract_destroy(contract_t *contract) {
  tools_destroy(&contract->python);
  tools_destroy(&contract->binary);
  jh_list_destroy(&contract->targets);
  jb_json_tree_destroy(contract->tree);
  memset(contract, 0, sizeof *contract);
}

static bool hex64(jb_json_span_t text) {
  if (text.length != 64)
    return false;
  for (size_t i = 0; i < 64; ++i)
    if (!jh_ascii_is_digit(text.data[i]) &&
        !(text.data[i] >= 'a' && text.data[i] <= 'f'))
      return false;
  return true;
}

static bool text_value(jb_json_tree_view_t object, const char *key,
                       jb_json_span_t *out) {
  return jh_json_string(jh_json_field(object, key), out) && out->length != 0;
}

/* _snapshot_tool_entries */
static bool snapshot_tools(jh_esp_t *e, jb_json_tree_view_t snapshot,
                           const char *key, bool source_license, tools_t *out) {
  const jb_json_tree_view_t raw = jh_json_field(snapshot, key);
  if (jh_json_type(raw) != JB_JSON_ARRAY || jh_json_count(raw) == 0)
    return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot has no "),
                    jh_span(key));
  jb_json_tree_view_t normalization = {0};
  const bool has_normalization =
      jh_json_member(snapshot, "licenseNormalization", &normalization);
  bool valid =
      !has_normalization || jh_json_type(normalization) == JB_JSON_OBJECT;
  for (size_t i = 0; valid && i < jh_json_count(normalization); ++i) {
    const jb_json_tree_view_t member = jh_json_item(normalization, i);
    jb_json_span_t value = {"", 0};
    valid = jh_json_key(member).length != 0 && jh_json_string(member, &value) &&
            value.length != 0;
  }
  if (!valid)
    return ESP_FAIL(e,
                    jh_span("ESP-IDF supply-chain snapshot has invalid license "
                            "normalization"));
  for (size_t i = 0; i < jh_json_count(raw) && esp_ok(e); ++i) {
    const jb_json_tree_view_t entry = jh_json_item(raw, i);
    jb_json_span_t name = {0}, version = {0}, license = {0}, upstream = {0},
                   source = {0};
    if (jh_json_type(entry) != JB_JSON_OBJECT ||
        !text_value(entry, "name", &name) ||
        !text_value(entry, "version", &version) ||
        !text_value(entry, "license", &license) ||
        !text_value(entry, "upstream", &upstream) ||
        (source_license && !text_value(entry, "sourceLicense", &source)))
      return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot "),
                      jh_span(key), jh_span("["), jh_span(esp_decimal(e, i)),
                      jh_span("] is invalid"));
    if (jh_list_contains(&out->names, name))
      return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot repeats "),
                      jh_span(key), jh_span(" entry "), esp_repr(e, name));
    if (source_license) {
      jb_json_span_t expected = source;
      jb_json_tree_view_t mapped = {0};
      if (jh_json_member(normalization, ESP_TEXT(e, source), &mapped))
        (void)jh_json_string(mapped, &expected);
      if (!jh_span_equal(license, expected))
        return ESP_FAIL(
            e,
            jh_span("ESP-IDF supply-chain snapshot license mismatch "
                    "for "),
            name, jh_span(": expected "), esp_repr(e, expected),
            jh_span(", found "), esp_repr(e, license));
    }
    (void)esp_push(e, &out->names, name);
    (void)esp_push(e, &out->versions, version);
    (void)esp_push(e, &out->licenses, license);
    (void)esp_push(e, &out->upstreams, upstream);
    (void)esp_push(e, &out->sources, source);
  }
  return esp_ok(e);
}

static bool load_contract(jh_esp_t *e, contract_t *c) {
  const char *path = esp_join(e, e->repo_root, SNAPSHOT);
  c->tree = esp_load_json(e, path, true, &c->snapshot);
  if (!esp_ok(e))
    return false;
  if (!jh_json_int64(jh_json_field(c->snapshot, "schemaVersion"), &c->schema) ||
      c->schema != 2)
    return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot has unsupported "
                               "schema"));
  const jb_json_tree_view_t idf = jh_json_field(c->snapshot, "espIdf");
  if (jh_json_type(idf) != JB_JSON_OBJECT)
    return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot has no espIdf "
                               "contract"));
  const char *keys[] = {"version", "commit"};
  const char *expected[] = {e->idf_version, e->idf_commit};
  for (size_t i = 0; i < COUNTOF(keys); ++i) {
    jb_json_tree_view_t value = {0};
    jb_json_span_t text = {0};
    const bool has = jh_json_member(idf, keys[i], &value);
    if (!has || !jh_json_string(value, &text) ||
        !jh_span_equal(text, jh_span(expected[i])))
      return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot "),
                      jh_span(keys[i]), jh_span(" mismatch: expected "),
                      esp_repr(e, jh_span(expected[i])), jh_span(", found "),
                      has ? esp_repr_tree(e, value) : jh_span("None"));
  }
  const jb_json_tree_view_t targets = jh_json_field(idf, "targets");
  bool valid = jh_json_type(targets) == JB_JSON_ARRAY;
  for (size_t i = 0; valid && i < jh_json_count(targets); ++i) {
    jb_json_span_t text = {0};
    valid = jh_json_string(jh_json_item(targets, i), &text) &&
            text.length != 0 && !jh_list_contains(&c->targets, text) &&
            esp_push(e, &c->targets, text);
  }
  if (!valid)
    return ESP_FAIL(
        e, jh_span("ESP-IDF supply-chain snapshot has invalid targets"));
  if (!jh_list_contains(&c->targets, jh_span(e->model.idf_target)))
    return ESP_FAIL(e,
                    jh_span("ESP-IDF supply-chain snapshot does not cover "
                            "target "),
                    esp_repr(e, jh_span(e->model.idf_target)));
  jb_json_span_t digest = {0};
  if (!jh_json_string(jh_json_field(idf, "toolsJsonSha256"), &digest) ||
      !hex64(digest))
    return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot has invalid "
                               "tools.json digest"));
  c->tools_digest = ESP_TEXT(e, digest);
  if (!snapshot_tools(e, c->snapshot, "binaryTools", true, &c->binary) ||
      !snapshot_tools(e, c->snapshot, "pythonTools", false, &c->python))
    return false;
  if (!jh_list_contains(&c->python.names, jh_span("esptool")))
    return ESP_FAIL(e, jh_span("ESP-IDF supply-chain snapshot has no esptool "
                               "Python distribution"));
  return esp_sha256_file(e, path, c->sha256);
}

/* The provenance part of the contract; python_versions replaces the
 * versions of the Python tools when given (the installed ones). */
static void write_tools(esp_json_t *json, const char *key, const tools_t *tools,
                        const jb_vector_t *versions) {
  esp_json_array(json, key);
  for (size_t i = 0; i < jb_vector_size(&tools->names); ++i) {
    esp_json_object(json, NULL);
    esp_json_string(json, "name", jh_string_span(jh_list_at(&tools->names, i)));
    esp_json_string(
        json, "version",
        jh_string_span(jh_list_at(versions ? versions : &tools->versions, i)));
    esp_json_string(json, "license",
                    jh_string_span(jh_list_at(&tools->licenses, i)));
    esp_json_object_end(json);
  }
  esp_json_array_end(json);
}

static void write_supply_chain(esp_json_t *json, const contract_t *c,
                               const jb_vector_t *python_versions) {
  esp_json_object(json, "supplyChain");
  esp_json_object(json, "snapshot");
  esp_json_string(json, "path", jh_span(SNAPSHOT));
  esp_json_u64(json, "schemaVersion", (uint64_t)c->schema);
  esp_json_string(json, "sha256", jh_span(c->sha256));
  esp_json_object_end(json);
  write_tools(json, "binaryTools", &c->binary, NULL);
  write_tools(json, "pythonTools", &c->python, python_versions);
  esp_json_object_end(json);
}

static const char *compiler_tool(jh_esp_t *e) {
  jb_json_span_t tool = {"", 0};
  (void)jh_json_string(
      jh_json_field(esp_table(e, "compilerTools"), e->model.idf_target), &tool);
  return ESP_TEXT(e, tool);
}

static size_t tool_index(const tools_t *tools, const char *name) {
  for (size_t i = 0; i < jb_vector_size(&tools->names); ++i)
    if (strcmp(esp_at(&tools->names, i), name) == 0)
      return i;
  return SIZE_MAX;
}

/* _normalized_lf_sha256 */
static bool lf_sha256(jh_esp_t *e, const char *path, char out[65]) {
  jb_file_data_t data = {0};
  if (!esp_read(e, path, &data))
    return false;
  size_t length = 0;
  for (size_t i = 0; i < data.length; ++i)
    if (!(data.data[i] == '\r' && i + 1 < data.length &&
          data.data[i + 1] == '\n'))
      data.data[length++] = data.data[i];
  jh_sha256_data_hex(data.data, length, out);
  jb_file_release(&data);
  return true;
}

static bool sorted_repr_set(jh_esp_t *e, jb_vector_t *list,
                            jb_json_span_t *out) {
  jh_list_sort(list);
  *out = esp_repr_list(e, list);
  return esp_ok(e);
}

/* verify_esp_idf_tools_contract with validate_binary_tools_contract. */
static jb_json_tree_t *verify_tools(jh_esp_t *e, const contract_t *c,
                                    jb_json_tree_view_t *document) {
  const char *path = esp_join(e, e->idf_dir, "tools/tools.json");
  if (!esp_ok(e))
    return NULL;
  if (!esp_is_file(path)) {
    (void)ESP_FAIL(e, jh_span("ESP-IDF tool registry is missing: "),
                   jh_span(path));
    return NULL;
  }
  char digest[65] = {0};
  if (!lf_sha256(e, path, digest))
    return NULL;
  if (strcmp(digest, c->tools_digest) != 0) {
    (void)ESP_FAIL(
        e, jh_span("ESP-IDF tools/tools.json digest drift: expected "),
        jh_span(c->tools_digest), jh_span(", found "), jh_span(digest));
    return NULL;
  }
  jb_json_tree_t *tree = esp_load_json(e, path, true, document);
  if (tree == NULL)
    return NULL;
  const jb_json_tree_view_t tools = jh_json_field(*document, "tools");
  jb_vector_t names = {0}, required = {0}, snapshot = {0}, recommended = {0};
  if (!esp_list_init(e, &names) || !esp_list_init(e, &required) ||
      !esp_list_init(e, &snapshot) || !esp_list_init(e, &recommended))
    goto done;
  if (jh_json_type(tools) != JB_JSON_ARRAY) {
    (void)ESP_FAIL(e, jh_span("ESP-IDF tools.json has no tools array"));
    goto done;
  }
  for (size_t i = 0; i < jh_json_count(tools) && esp_ok(e); ++i) {
    const jb_json_tree_view_t tool = jh_json_item(tools, i);
    jb_json_span_t name = {0};
    if (jh_json_type(tool) != JB_JSON_OBJECT ||
        !jh_json_string(jh_json_field(tool, "name"), &name)) {
      (void)ESP_FAIL(e, jh_span("ESP-IDF tools.json contains an invalid tool"));
      break;
    }
    if (jh_list_contains(&names, name)) {
      (void)ESP_FAIL(e, jh_span("ESP-IDF tools.json repeats tool "),
                     esp_repr(e, name));
      break;
    }
    (void)esp_push(e, &names, name);
    jb_json_span_t install = {0};
    const jb_json_tree_view_t supported =
        jh_json_field(tool, "supported_targets");
    if (!jh_json_string(jh_json_field(tool, "install"), &install) ||
        !jh_span_equal(install, jh_span("always")) ||
        jh_json_type(supported) != JB_JSON_ARRAY)
      continue;
    bool covered = false;
    for (size_t j = 0; j < jh_json_count(supported) && !covered; ++j) {
      jb_json_span_t target = {0};
      covered = jh_json_string(jh_json_item(supported, j), &target) &&
                (jh_span_equal(target, jh_span("all")) ||
                 jh_list_contains(&c->targets, target));
    }
    if (covered)
      (void)esp_push_unique(e, &required, name);
  }
  for (size_t i = 0; i < jb_vector_size(&c->binary.names) && esp_ok(e); ++i)
    (void)esp_push_unique(e, &snapshot,
                          jh_string_span(jh_list_at(&c->binary.names, i)));
  jh_list_sort(&required);
  jh_list_sort(&snapshot);
  bool same = jb_vector_size(&required) == jb_vector_size(&snapshot);
  for (size_t i = 0; same && i < jb_vector_size(&required); ++i)
    same = strcmp(esp_at(&required, i), esp_at(&snapshot, i)) == 0;
  if (esp_ok(e) && !same) {
    jb_json_span_t a = {0}, b = {0};
    (void)sorted_repr_set(e, &required, &a);
    (void)sorted_repr_set(e, &snapshot, &b);
    (void)ESP_FAIL(e,
                   jh_span("ESP-IDF supply-chain snapshot binary tool coverage "
                           "drift: expected "),
                   a, jh_span(", found "), b);
    goto done;
  }
  for (size_t i = 0; i < jb_vector_size(&c->binary.names) && esp_ok(e); ++i) {
    const char *name = esp_at(&c->binary.names, i);
    jb_json_tree_view_t actual = {0};
    for (size_t j = 0; j < jh_json_count(tools); ++j) {
      jb_json_span_t text = {0};
      if (jh_json_string(jh_json_field(jh_json_item(tools, j), "name"),
                         &text) &&
          jh_span_equal(text, jh_span(name)))
        actual = jh_json_item(tools, j);
    }
    if (actual.tree == NULL) {
      (void)ESP_FAIL(e, jh_span("ESP-IDF tools.json has no binary tool "),
                     esp_repr(e, jh_span(name)));
      break;
    }
    jh_list_destroy(&recommended);
    (void)jh_list_init(&recommended);
    const jb_json_tree_view_t versions = jh_json_field(actual, "versions");
    for (size_t j = 0; jh_json_type(versions) == JB_JSON_ARRAY &&
                       j < jh_json_count(versions) && esp_ok(e);
         ++j) {
      const jb_json_tree_view_t item = jh_json_item(versions, j);
      jb_json_span_t status = {0}, version = {0};
      if (jh_json_type(item) != JB_JSON_OBJECT ||
          !jh_json_string(jh_json_field(item, "status"), &status) ||
          !jh_span_equal(status, jh_span("recommended")))
        continue;
      jb_json_tree_view_t value = {0};
      if (!jh_json_member(item, "name", &value))
        version = jh_span("None");
      else if (!jh_json_string(value, &version))
        version = esp_repr_tree(e, value);
      (void)esp_push_unique(e, &recommended, version);
    }
    const jb_json_span_t version =
        jh_string_span(jh_list_at(&c->binary.versions, i));
    if (!jh_list_contains(&recommended, version)) {
      jb_json_span_t found = {0};
      (void)sorted_repr_set(e, &recommended, &found);
      (void)ESP_FAIL(e, jh_span("ESP-IDF binary tool "),
                     esp_repr(e, jh_span(name)),
                     jh_span(" recommended version drift: expected "),
                     esp_repr(e, version), jh_span(", found "), found);
      break;
    }
    const char *fields[] = {"license", "info_url"};
    const jb_vector_t *wanted[] = {&c->binary.sources, &c->binary.upstreams};
    const char *labels[] = {" source license drift: expected ",
                            " upstream drift: expected "};
    for (size_t f = 0; f < COUNTOF(fields) && esp_ok(e); ++f) {
      jb_json_tree_view_t value = {0};
      jb_json_span_t text = {0};
      const jb_json_span_t expected = jh_string_span(jh_list_at(wanted[f], i));
      const bool has = jh_json_member(actual, fields[f], &value);
      if (has && jh_json_string(value, &text) && jh_span_equal(text, expected))
        continue;
      (void)ESP_FAIL(e, jh_span("ESP-IDF binary tool "),
                     esp_repr(e, jh_span(name)), jh_span(labels[f]),
                     esp_repr(e, expected), jh_span(", found "),
                     has ? esp_repr_tree(e, value) : jh_span("None"));
    }
  }
done:
  jh_list_destroy(&recommended);
  jh_list_destroy(&snapshot);
  jh_list_destroy(&required);
  jh_list_destroy(&names);
  if (!esp_ok(e)) {
    jb_json_tree_destroy(tree);
    return NULL;
  }
  return tree;
}

/* Path.name without ".exe", lower case. */
static const char *program_name(jh_esp_t *e, const char *path) {
  jb_string_t text = {0};
  const char *name = esp_name(path);
  size_t length = strlen(name);
  if (length >= 4 && strcmp(name + length - 4, ".exe") == 0)
    length -= 4;
  if (!esp_storage(e, jb_string_init(&text, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_assign(&text, name, length))) {
    jb_string_destroy(&text);
    return "";
  }
  for (char *c = jb_string_mutable_data(&text); *c != '\0'; ++c)
    if (*c >= 'A' && *c <= 'Z')
      *c = (char)(*c - 'A' + 'a');
  const char *result = ESP_TEXT(e, jh_string_span(&text));
  jb_string_destroy(&text);
  return result;
}

/* validate_compiler_contract */
static bool compiler_contract(jh_esp_t *e, const char *compiler,
                              jb_json_span_t output,
                              jb_json_tree_view_t document, const contract_t *c,
                              const char **tool, const char **tool_version) {
  const char *name = compiler_tool(e);
  if (esp_ok(e) && name[0] == '\0')
    return ESP_FAIL(e,
                    jh_span("No compiler supply-chain contract for ESP-IDF "
                            "target "),
                    esp_repr(e, jh_span(e->model.idf_target)));
  const size_t index = tool_index(&c->binary, name);
  jb_json_tree_view_t actual = {0};
  const jb_json_tree_view_t tools = jh_json_field(document, "tools");
  for (size_t i = 0; i < jh_json_count(tools) && actual.tree == NULL; ++i) {
    jb_json_span_t text = {0};
    const jb_json_tree_view_t item = jh_json_item(tools, i);
    if (jh_json_type(item) == JB_JSON_OBJECT &&
        jh_json_string(jh_json_field(item, "name"), &text) &&
        jh_span_equal(text, jh_span(name)))
      actual = item;
  }
  if (index == SIZE_MAX || actual.tree == NULL)
    return ESP_FAIL(e, jh_span("ESP-IDF compiler tool "),
                    esp_repr(e, jh_span(name)),
                    jh_span(" is missing from the contract"));
  const jb_json_tree_view_t command = jh_json_field(actual, "version_cmd");
  jb_json_span_t first = {0}, pattern = {0};
  if (jh_json_type(command) != JB_JSON_ARRAY || jh_json_count(command) == 0 ||
      !jh_json_string(jh_json_item(command, 0), &first) ||
      !jh_json_string(jh_json_field(actual, "version_regex"), &pattern) ||
      pattern.length == 0)
    return ESP_FAIL(e, jh_span("ESP-IDF compiler tool "),
                    esp_repr(e, jh_span(name)),
                    jh_span(" has no version probe"));
  const char *official = program_name(e, ESP_TEXT(e, first));
  const char *target_name = official;
  const char *marker = strstr(official, "-esp-");
  if (marker != NULL)
    target_name =
        ESP_TEXT(e, ((jb_json_span_t){official, (size_t)(marker - official)}),
                 jh_span("-"), jh_span(e->model.idf_target), jh_span("-"),
                 jh_span(marker + 5));
  const char *executed = program_name(e, compiler);
  if (esp_ok(e) && strcmp(executed, official) != 0 &&
      strcmp(executed, target_name) != 0)
    return ESP_FAIL(e, jh_span("Executed C compiler "),
                    esp_repr(e, jh_span(esp_name(compiler))),
                    jh_span(" does not match ESP-IDF tool "),
                    esp_repr(e, jh_span(name)));
  jb_json_span_t version = {0};
  size_t groups = 0;
  const bool matched =
      esp_search(e, ESP_TEXT(e, pattern), output, false, &version, &groups);
  if (!esp_ok(e))
    return false;
  if (!matched || groups == 0)
    return ESP_FAIL(e, jh_span("Cannot parse ESP-IDF compiler tool version: "),
                    esp_repr(e, output));
  const jb_json_span_t expected =
      jh_string_span(jh_list_at(&c->binary.versions, index));
  if (!jh_span_equal(version, expected))
    return ESP_FAIL(e, jh_span("Executed C compiler version drift: expected "),
                    esp_repr(e, expected), jh_span(", found "),
                    esp_repr(e, version));
  *tool = name;
  *tool_version = ESP_TEXT(e, version);
  return esp_ok(e);
}

const char *esp_idf_python(jh_esp_t *e) {
  const char *root =
      esp_environment_get(&e->environment, "IDF_PYTHON_ENV_PATH");
  if (root == NULL)
    root = "";
#ifdef _WIN32
  return esp_join(e, esp_join(e, root, "Scripts"), "python.exe");
#else
  return esp_join(e, esp_join(e, root, "bin"), "python");
#endif
}

/* _tool_version: the first line of the stripped stdout and stderr. */
static const char *tool_version(jh_esp_t *e, const jb_vector_t *command,
                                const char *display) {
  jb_string_t out = {0}, err = {0}, text = {0}, decoded = {0};
  jb_vector_t arguments = {0};
  const char *result = "";
  if (!esp_storage(e, jb_string_init(&out, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&err, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&text, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&decoded, jb_heap_allocator())) ||
      !esp_list_init(e, &arguments))
    goto done;
  for (size_t i = 1; i < jb_vector_size(command); ++i)
    (void)esp_push(e, &arguments, jh_string_span(jh_list_at(command, i)));
  const char *program = esp_at(command, 0);
  const esp_process_t process = {program,
                                 &arguments,
                                 NULL,
                                 &e->environment,
                                 ESP_RUN_CAPTURE,
                                 false,
                                 true,
                                 0,
                                 &out,
                                 &err,
                                 0};
  int code = -1;
  const hal_status_t status = esp_process(e, &process, &code);
  if (!esp_ok(e))
    goto done;
  if (status == HAL_ENOENT) {
    (void)ESP_FAIL(e, jh_span("[Errno 2] No such file or directory: "),
                   esp_repr(e, jh_span(program)));
    goto done;
  }
  if (status != HAL_OK) {
    (void)jh_fail(e->failure, status, 1,
                  JH_PARTS(jh_span("cannot run "), jh_span(program),
                           jh_span(": "),
                           jh_span(hal_status_to_string(status))));
    goto done;
  }
  if (!esp_storage(e, jb_string_append(&out, jb_string_data(&err),
                                       jb_string_size(&err))) ||
      !esp_decode_text(e, jh_string_span(&out), &decoded))
    goto done;
  const jb_json_span_t stripped = esp_strip(jh_string_span(&decoded));
  if (code != 0 || stripped.length == 0) {
    char number[16];
    (void)snprintf(number, sizeof number, "%d", code);
    (void)ESP_FAIL(e, jh_span("Cannot query "), jh_span(display),
                   jh_span(" version (exit "), jh_span(number), jh_span(")"));
    goto done;
  }
  result = ESP_TEXT(e, esp_strip(esp_first_line(stripped)));
done:
  jh_list_destroy(&arguments);
  jb_string_destroy(&decoded);
  jb_string_destroy(&text);
  jb_string_destroy(&err);
  jb_string_destroy(&out);
  return result;
}

static const char *version_of(jh_esp_t *e, const char *program,
                              const char *display) {
  jb_vector_t list = {0};
  const char *result = "";
  if (esp_list_init(e, &list) && esp_push(e, &list, jh_span(program)) &&
      esp_push(e, &list, jh_span("--version")))
    result = tool_version(e, &list, display);
  jh_list_destroy(&list);
  return result;
}

/* _version_match */
static const char *version_match(jh_esp_t *e, const char *output,
                                 const char *pattern, const char *display) {
  jb_json_span_t group = {0};
  if (!esp_ok(e))
    return "";
  if (!esp_search(e, pattern, jh_span(output), true, &group, NULL)) {
    if (esp_ok(e))
      (void)ESP_FAIL(e, jh_span("Cannot parse "), jh_span(display),
                     jh_span(" version output: "),
                     esp_repr(e, jh_span(output)));
    return "";
  }
  return ESP_TEXT(e, group);
}

/* collect_python_tools */
static bool python_tools(jh_esp_t *e, const char *python, const contract_t *c,
                         jb_vector_t *versions) {
  static const char program[] =
      "import importlib.metadata as metadata\n"
      "import json\n"
      "import sys\n"
      "names = json.loads(sys.argv[1])\n"
      "versions = {}\n"
      "missing = []\n"
      "for name in names:\n"
      "    try:\n"
      "        versions[name] = metadata.version(name)\n"
      "    except metadata.PackageNotFoundError:\n"
      "        missing.append(name)\n"
      "print(json.dumps({'missing': missing, 'versions': versions}, "
      "sort_keys=True))";
  jb_vector_t arguments = {0};
  jb_string_t out = {0}, err = {0}, names = {0};
  jb_json_tree_t *tree = NULL;
  hal_status_t status = HAL_OK;
  if (!esp_list_init(e, &arguments) ||
      !esp_storage(e, jb_string_init(&out, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&err, jb_heap_allocator())) ||
      !esp_storage(e, jb_string_init(&names, jb_heap_allocator())))
    goto done;
  /* json.dumps(names, separators=(",", ":")) */
  jh_put(&names, &status, "[");
  for (size_t i = 0; i < jb_vector_size(&c->python.names) && status == HAL_OK;
       ++i) {
    jh_put(&names, &status, i != 0 ? ",\"" : "\"");
    for (const char *ch = esp_at(&c->python.names, i); *ch != '\0'; ++ch) {
      if (*ch == '"' || *ch == '\\')
        jh_put(&names, &status, "\\");
      if (status == HAL_OK)
        status = jb_string_append(&names, ch, 1);
    }
    jh_put(&names, &status, "\"");
  }
  jh_put(&names, &status, "]");
  if (!esp_storage(e, status) || !esp_push(e, &arguments, jh_span("-c")) ||
      !esp_push(e, &arguments, jh_span(program)) ||
      !esp_push(e, &arguments, jh_string_span(&names)))
    goto done;
  const esp_process_t process = {python,
                                 &arguments,
                                 NULL,
                                 &e->environment,
                                 ESP_RUN_CAPTURE,
                                 false,
                                 true,
                                 0,
                                 &out,
                                 &err,
                                 0};
  int code = -1;
  status = esp_process(e, &process, &code);
  if (!esp_ok(e))
    goto done;
  if (status == HAL_ENOENT) {
    (void)ESP_FAIL(e, jh_span("[Errno 2] No such file or directory: "),
                   esp_repr(e, jh_span(python)));
    goto done;
  }
  if (status != HAL_OK || code != 0) {
    char number[16];
    (void)snprintf(number, sizeof number, "%d", code);
    (void)ESP_FAIL(e, jh_span("Cannot query IDF Python distributions (exit "),
                   jh_span(number), jh_span(")"));
    goto done;
  }
  jb_json_tree_view_t root = {0};
  jh_failure_t ignored;
  jh_failure_init(&ignored);
  jh_failure_t *saved = e->failure;
  e->failure = &ignored;
  tree = esp_parse_text(e, esp_strip(jh_string_span(&out)), &root);
  e->failure = saved;
  jh_failure_destroy(&ignored);
  if (tree == NULL) {
    (void)ESP_FAIL(e, jh_span("Cannot parse IDF Python distribution versions"));
    goto done;
  }
  const jb_json_tree_view_t found = jh_json_field(root, "versions");
  const jb_json_tree_view_t missing = jh_json_field(root, "missing");
  if (jh_json_type(root) != JB_JSON_OBJECT ||
      jh_json_type(found) != JB_JSON_OBJECT ||
      jh_json_type(missing) != JB_JSON_ARRAY) {
    (void)ESP_FAIL(e,
                   jh_span("Invalid IDF Python distribution version response"));
    goto done;
  }
  if (jh_json_count(missing) != 0) {
    jb_vector_t names_missing = {0};
    if (esp_list_init(e, &names_missing)) {
      for (size_t i = 0; i < jh_json_count(missing); ++i) {
        jb_json_span_t text = {0};
        const jb_json_tree_view_t item = jh_json_item(missing, i);
        (void)esp_push(e, &names_missing,
                       jh_json_string(item, &text) ? text
                                                   : esp_repr_tree(e, item));
      }
      jh_list_sort(&names_missing);
      (void)ESP_FAIL(
          e, jh_span("IDF Python environment is missing distributions: "),
          jh_span(esp_join_list(e, &names_missing, ", ")));
    }
    jh_list_destroy(&names_missing);
    goto done;
  }
  for (size_t i = 0; i < jb_vector_size(&c->python.names) && esp_ok(e); ++i) {
    const char *name = esp_at(&c->python.names, i);
    const jb_json_span_t expected =
        jh_string_span(jh_list_at(&c->python.versions, i));
    jb_json_tree_view_t value = {0};
    jb_json_span_t text = {0};
    const bool has = jh_json_member(found, name, &value);
    if (!has || !jh_json_string(value, &text) ||
        !jh_span_equal(text, expected)) {
      (void)ESP_FAIL(
          e, jh_span("IDF Python distribution "), esp_repr(e, jh_span(name)),
          jh_span(" version drift: expected "), esp_repr(e, expected),
          jh_span(", found "), has ? esp_repr_tree(e, value) : jh_span("None"));
      break;
    }
    (void)esp_push(e, versions, text);
  }
done:
  jb_json_tree_destroy(tree);
  jb_string_destroy(&names);
  jb_string_destroy(&err);
  jb_string_destroy(&out);
  jh_list_destroy(&arguments);
  return esp_ok(e);
}

/* _load_cmake_cache of the build directory, as `jh firmware read` reads it. */
static bool cmake_cache(jh_esp_t *e, jb_file_data_t *cache) {
  const char *path = esp_join(e, e->build_dir, "CMakeCache.txt");
  if (!esp_ok(e))
    return false;
  const hal_status_t status = jb_file_read(path, JH_ESP_MAX_BYTES, cache);
  if (status != HAL_OK)
    return ESP_FAIL(e, jh_span("Cannot read CMake cache "), jh_span(path),
                    jh_span(": "), jh_span(hal_status_to_string(status)));
  return true;
}

static const char *cache_value(jh_esp_t *e, const jb_file_data_t *cache,
                               const char *key) {
  jb_json_span_t value = {"", 0};
  bool found = false;
  const hal_status_t status = jh_cmake_cache_find(
      (jb_json_span_t){(const char *)cache->data, cache->length}, key, &value,
      &found);
  if (status != HAL_OK) {
    (void)ESP_FAIL(e,
                   jh_span("CMake cache has a duplicate or malformed entry "),
                   jh_span(key));
    return "";
  }
  return ESP_TEXT(e, value);
}

bool esp_collect_toolchain(jh_esp_t *e) {
  jb_json_tree_view_t description = {0}, document = {0};
  jb_json_tree_t *description_tree =
      esp_load_json(e, esp_join(e, e->build_dir, "project_description.json"),
                    true, &description);
  contract_t c;
  contract_init(&c);
  jb_json_tree_t *tools_tree = NULL;
  jb_file_data_t cache = {0};
  jb_vector_t versions = {0};
  esp_json_t json = {0};
  bool json_started = false;
  if (!esp_ok(e) || !esp_list_init(e, &versions))
    goto done;
  jb_json_span_t compiler_text = {0};
  if (!text_value(description, "c_compiler", &compiler_text)) {
    (void)ESP_FAIL(e, jh_span("project_description.json has no C compiler"));
    goto done;
  }
  const char *compiler = ESP_TEXT(e, compiler_text);
  if (!esp_is_file(compiler)) {
    (void)ESP_FAIL(e, jh_span("ESP-IDF C compiler is missing: "),
                   jh_span(compiler));
    goto done;
  }
  if (!load_contract(e, &c) ||
      (tools_tree = verify_tools(e, &c, &document)) == NULL ||
      !cmake_cache(e, &cache))
    goto done;
  if (strcmp(cache_value(e, &cache, "CMAKE_GENERATOR"), "Ninja") != 0) {
    (void)ESP_FAIL(e, jh_span("ESP-IDF production builds require the Ninja "
                              "generator"));
    goto done;
  }
  const char *cmake = cache_value(e, &cache, "CMAKE_COMMAND");
  const char *ninja = cache_value(e, &cache, "CMAKE_MAKE_PROGRAM");
  if (esp_ok(e) && (!esp_is_file(cmake) || !esp_is_file(ninja))) {
    (void)ESP_FAIL(e,
                   jh_span("CMake cache does not identify the executed build "
                           "tools"));
    goto done;
  }
  const char *python = esp_idf_python(e);
  const char *compiler_output = version_of(e, compiler, "C compiler");
  const char *tool = "", *tool_version_text = "";
  if (!esp_ok(e) || !compiler_contract(e, compiler, jh_span(compiler_output),
                                       document, &c, &tool, &tool_version_text))
    goto done;
  const char *cmake_output = version_of(e, cmake, "CMake");
  const char *ninja_output = version_of(e, ninja, "Ninja");
  const char *python_output = version_of(e, python, "IDF Python");
  jb_vector_t esptool_command = {0};
  const char *esptool_output = "";
  if (esp_list_init(e, &esptool_command) &&
      esp_push(e, &esptool_command, jh_span(python)) &&
      esp_push(e, &esptool_command, jh_span("-m")) &&
      esp_push(e, &esptool_command, jh_span("esptool")) &&
      esp_push(e, &esptool_command, jh_span("version")))
    esptool_output = tool_version(e, &esptool_command, "esptool");
  jh_list_destroy(&esptool_command);
  if (!esp_ok(e) || !python_tools(e, python, &c, &versions))
    goto done;
  const char *esptool = version_match(
      e, esptool_output,
      "esptool(?:\\.py)?(?:\\s+version)?\\s+v?([0-9][0-9A-Za-z.+-]*)",
      "esptool");
  const char *expected_esptool =
      esp_at(&c.python.versions, tool_index(&c.python, "esptool"));
  if (esp_ok(e) && strcmp(esptool, expected_esptool) != 0) {
    (void)ESP_FAIL(e, jh_span("esptool command version drift: expected "),
                   esp_repr(e, jh_span(expected_esptool)), jh_span(", found "),
                   esp_repr(e, jh_span(esptool)));
    goto done;
  }
  const char *cmake_version = version_match(
      e, cmake_output, "cmake\\s+version\\s+([0-9][0-9A-Za-z.+-]*)", "CMake");
  const char *ninja_version =
      version_match(e, ninja_output, "^([0-9][0-9A-Za-z.+-]*)$", "Ninja");
  const char *python_version = version_match(
      e, python_output, "python\\s+([0-9][0-9A-Za-z.+-]*)", "IDF Python");
  char tools_digest[65] = {0};
  if (!esp_ok(e) ||
      !lf_sha256(e, esp_join(e, e->idf_dir, "tools/tools.json"),
                 tools_digest) ||
      !esp_json_begin(e, &json))
    goto done;
  json_started = true;
  esp_json_u64(&json, "schemaVersion", 3);
  esp_json_object(&json, "cCompiler");
  esp_json_string(&json, "name", jh_span(esp_name(compiler)));
  esp_json_string(&json, "version", jh_span(compiler_output));
  esp_json_string(&json, "tool", jh_span(tool));
  esp_json_string(&json, "toolVersion", jh_span(tool_version_text));
  esp_json_object_end(&json);
  const char *names[] = {"cmake", "ninja", "idfPython", "esptool"};
  const char *programs[] = {esp_name(cmake), esp_name(ninja), esp_name(python),
                            "esptool"};
  const char *found[] = {cmake_version, ninja_version, python_version, esptool};
  for (size_t i = 0; i < COUNTOF(names); ++i) {
    esp_json_object(&json, names[i]);
    esp_json_string(&json, "name", jh_span(programs[i]));
    esp_json_string(&json, "version", jh_span(found[i]));
    esp_json_object_end(&json);
  }
  esp_json_object(&json, "espIdfTools");
  esp_json_string(&json, "path", jh_span("tools/tools.json"));
  esp_json_string(&json, "sha256", jh_span(tools_digest));
  esp_json_object_end(&json);
  write_supply_chain(&json, &c, &versions);
  esp_json_end(&json);
  if (esp_storage(e, json.status))
    (void)esp_write_text(
        e,
        esp_join(e, esp_join(e, e->build_dir, JH_ESP_GENERATED_DIR),
                 TOOLCHAIN_JSON),
        jh_span(esp_dumps(e, jh_string_span(&json.text))));
done:
  (void)json_started;
  jb_string_destroy(&json.text);
  jh_list_destroy(&versions);
  jb_file_release(&cache);
  jb_json_tree_destroy(tools_tree);
  contract_destroy(&c);
  jb_json_tree_destroy(description_tree);
  return esp_ok(e);
}

/* ---- Artifact validation ---- */

/* _artifact_path: path below the build directory and its relative form. */
static bool artifact_path(jh_esp_t *e, const char *value, const char **path,
                          const char **relative) {
  const char *candidate =
      esp_absolute(value) ? value : esp_join(e, e->build_dir, value);
  const char *resolved = esp_resolve(e, candidate, e->options->base);
  if (!esp_ok(e))
    return false;
  const char *inside = esp_relative(e, resolved, e->build_dir);
  if (inside == NULL)
    return ESP_FAIL(e, jh_span("Artifact escapes the build directory: "),
                    jh_span(value));
  uint64_t size = 0;
  if (!esp_file_size(resolved, &size))
    return ESP_FAIL(e, jh_span("Missing build artifact: "), jh_span(resolved));
  if (size == 0)
    return ESP_FAIL(e, jh_span("Build artifact is empty: "), jh_span(resolved));
  *path = resolved;
  *relative = inside;
  return true;
}

static bool absolute_any(const char *text) {
#ifdef _WIN32
  return esp_absolute(text);
#else
  if (text[0] == '/')
    return true;
  /* PureWindowsPath(item).is_absolute(): a drive with a root, or UNC. */
  const bool drive = jh_ascii_is_upper(text[0]) || jh_ascii_is_lower(text[0]);
  if (drive && text[1] == ':' && (text[2] == '\\' || text[2] == '/'))
    return true;
  return (text[0] == '\\' || text[0] == '/') &&
         (text[1] == '\\' || text[1] == '/') && text[2] != '\0' &&
         text[2] != '\\' && text[2] != '/' && strpbrk(text + 2, "\\/") != NULL;
#endif
}

static bool contains_absolute(jh_esp_t *e, jb_json_tree_view_t value) {
  const jb_json_type_t type = jh_json_type(value);
  if (type == JB_JSON_STRING) {
    jb_json_span_t text = {0};
    (void)jh_json_string(value, &text);
    return absolute_any(ESP_TEXT(e, text));
  }
  if (type == JB_JSON_OBJECT || type == JB_JSON_ARRAY)
    for (size_t i = 0; i < jh_json_count(value); ++i)
      if (contains_absolute(e, jh_json_item(value, i)))
        return true;
  return false;
}

static bool version_text(jb_json_tree_view_t tool) {
  jb_json_span_t text = {0};
  if (!jh_json_string(jh_json_field(tool, "version"), &text) ||
      text.length == 0 || !jh_ascii_is_digit(text.data[0]))
    return false;
  for (size_t i = 1; i < text.length; ++i) {
    const char c = text.data[i];
    if (!jh_ascii_is_digit(c) && !jh_ascii_is_upper(c) &&
        !jh_ascii_is_lower(c) && c != '.' && c != '+' && c != '-')
      return false;
  }
  return true;
}

/* _validate_toolchain_provenance */
static bool validate_toolchain(jh_esp_t *e, jb_json_tree_view_t value,
                               const contract_t *c) {
  int64_t schema = 0;
  if (!jh_json_int64(jh_json_field(value, "schemaVersion"), &schema) ||
      schema != 3)
    return ESP_FAIL(e,
                    jh_span("Toolchain provenance has an unsupported schema"));
  const jb_json_tree_view_t compiler = jh_json_field(value, "cCompiler");
  const char *fields[] = {"name", "version", "tool", "toolVersion"};
  jb_json_span_t texts[COUNTOF(fields)] = {{0}};
  bool valid = jh_json_type(compiler) == JB_JSON_OBJECT;
  for (size_t i = 0; valid && i < COUNTOF(fields); ++i)
    valid = text_value(compiler, fields[i], &texts[i]);
  if (!valid)
    return ESP_FAIL(e,
                    jh_span("Toolchain provenance has no C compiler version"));
  const char *tool = compiler_tool(e);
  const size_t index =
      tool[0] != '\0' ? tool_index(&c->binary, tool) : SIZE_MAX;
  if (index == SIZE_MAX || !jh_span_equal(texts[2], jh_span(tool)) ||
      !jh_span_equal(texts[3],
                     jh_string_span(jh_list_at(&c->binary.versions, index))))
    return ESP_FAIL(e,
                    jh_span("Toolchain provenance C compiler differs from the "
                            "supply-chain snapshot"));
  const char *keys[] = {"cmake", "ninja", "idfPython", "esptool"};
  const char *displays[] = {"CMake", "Ninja", "IDF Python", "esptool"};
  for (size_t i = 0; i < COUNTOF(keys); ++i) {
    const jb_json_tree_view_t entry = jh_json_field(value, keys[i]);
    jb_json_span_t name = {0};
    if (jh_json_type(entry) != JB_JSON_OBJECT ||
        !text_value(entry, "name", &name) || !version_text(entry))
      return ESP_FAIL(e, jh_span("Toolchain provenance has no valid "),
                      jh_span(displays[i]), jh_span(" version"));
  }
  jb_json_span_t esptool = {0};
  (void)jh_json_string(jh_json_field(jh_json_field(value, "esptool"), "name"),
                       &esptool);
  if (!jh_span_equal(esptool, jh_span("esptool")))
    return ESP_FAIL(
        e, jh_span("Toolchain provenance has an invalid esptool name"));
  const jb_json_tree_view_t tools = jh_json_field(value, "espIdfTools");
  jb_json_span_t path = {0}, digest = {0};
  if (jh_json_type(tools) != JB_JSON_OBJECT ||
      !jh_json_string(jh_json_field(tools, "path"), &path) ||
      !jh_span_equal(path, jh_span("tools/tools.json")) ||
      !jh_json_string(jh_json_field(tools, "sha256"), &digest) ||
      !hex64(digest))
    return ESP_FAIL(e,
                    jh_span("Toolchain provenance has no valid ESP-IDF tools "
                            "digest"));
  if (!jh_span_equal(digest, jh_span(c->tools_digest)))
    return ESP_FAIL(e,
                    jh_span("Toolchain provenance ESP-IDF tools digest differs "
                            "from the supply-chain snapshot"));
  esp_json_t json;
  jb_json_tree_t *expected = NULL;
  jb_json_tree_view_t root = {0};
  if (esp_json_begin(e, &json)) {
    write_supply_chain(&json, c, NULL);
    esp_json_end(&json);
    if (esp_storage(e, json.status))
      expected = esp_parse_text(e, jh_string_span(&json.text), &root);
    jb_string_destroy(&json.text);
  }
  jb_json_tree_view_t actual = {0};
  const bool has = jh_json_member(value, "supplyChain", &actual);
  if (esp_ok(e) &&
      (!has || !esp_tree_equal(e, actual, jh_json_field(root, "supplyChain"))))
    (void)ESP_FAIL(e, jh_span("Toolchain provenance differs from the ESP-IDF "
                              "supply-chain snapshot"));
  jb_json_tree_destroy(expected);
  if (esp_ok(e) && contains_absolute(e, value))
    (void)ESP_FAIL(
        e, jh_span("Toolchain provenance contains an absolute host path"));
  return esp_ok(e);
}

/* _validate_clean_build_log */
static bool clean_log(jh_esp_t *e, const char *path) {
  jb_file_data_t data = {0};
  jb_string_t text = {0};
  jb_vector_t findings = {0};
  if (!esp_read(e, path, &data) ||
      !esp_storage(e, jb_string_init(&text, jb_heap_allocator())) ||
      !esp_list_init(e, &findings) ||
      !esp_decode_text(
          e, (jb_json_span_t){(const char *)data.data, data.length}, &text))
    goto done;
  jb_json_span_t input = jh_string_span(&text), line = {0};
  while (jh_metadata_next_line(&input, &line) && esp_ok(e)) {
    bool finding = false;
    for (size_t i = 0; i < line.length && !finding; ++i) {
      if (i != 0 && !(line.data[i - 1] == ' ' ||
                      (line.data[i - 1] >= 0x09 && line.data[i - 1] <= 0x0D)))
        continue;
      static const char *const words[] = {"warning:", "error:"};
      for (size_t w = 0; w < COUNTOF(words) && !finding; ++w) {
        const size_t n = strlen(words[w]);
        bool same = i + n <= line.length;
        for (size_t k = 0; same && k < n; ++k)
          same = (char)(line.data[i + k] | 0x20) == words[w][k] ||
                 line.data[i + k] == words[w][k];
        finding = same;
      }
    }
    static const char kconfig[] = "unknown kconfig";
    for (size_t i = 0; !finding && i + sizeof kconfig - 1 <= line.length; ++i) {
      bool same = true;
      for (size_t k = 0; same && k < sizeof kconfig - 1; ++k) {
        char c = line.data[i + k];
        if (c >= 'A' && c <= 'Z')
          c = (char)(c - 'A' + 'a');
        same = c == kconfig[k];
      }
      finding = same;
    }
    if (finding && jb_vector_size(&findings) < 8)
      (void)esp_push(e, &findings, line);
    else if (finding)
      break;
  }
  if (esp_ok(e) && jb_vector_size(&findings) != 0)
    (void)ESP_FAIL(e, jh_span("ESP-IDF build log contains diagnostics:\n"),
                   jh_span(esp_join_list(e, &findings, "\n")));
done:
  jh_list_destroy(&findings);
  jb_string_destroy(&text);
  jb_file_release(&data);
  return esp_ok(e);
}

/* _load_sdkconfig and _partition_configuration */
static bool partition_configuration(jh_esp_t *e, const char *sdkconfig_path,
                                    const jb_vector_t *offsets,
                                    const jb_vector_t *paths,
                                    const jb_vector_t *digests,
                                    char sdkconfig[65], size_t *partition,
                                    const char **profile) {
  jb_file_data_t data = {0};
  jb_string_t text = {0};
  if (!esp_read(e, sdkconfig_path, &data) ||
      !esp_storage(e, jb_string_init(&text, jb_heap_allocator())) ||
      !esp_decode_text(
          e, (jb_json_span_t){(const char *)data.data, data.length}, &text)) {
    jb_string_destroy(&text);
    jb_file_release(&data);
    return false;
  }
  jh_sha256_data_hex(data.data, data.length, sdkconfig);
  jb_file_release(&data);
  const char *active = NULL;
  size_t active_count = 0;
  const char *offset_value = NULL;
  for (size_t p = 0; p < COUNTOF(partition_profiles); ++p) {
    jb_json_span_t input = jh_string_span(&text), line = {0};
    const char *value = NULL;
    const size_t n = strlen(partition_profiles[p]);
    while (jh_metadata_next_line(&input, &line))
      if (line.length > n && memcmp(line.data, partition_profiles[p], n) == 0 &&
          line.data[n] == '=')
        value = ESP_TEXT(
            e, ((jb_json_span_t){line.data + n + 1, line.length - n - 1}));
    if (value != NULL && strcmp(value, "y") == 0) {
      active = partition_profiles[p];
      ++active_count;
    }
  }
  {
    jb_json_span_t input = jh_string_span(&text), line = {0};
    static const char key[] = "CONFIG_PARTITION_TABLE_OFFSET=";
    while (jh_metadata_next_line(&input, &line))
      if (line.length >= sizeof key - 1 &&
          memcmp(line.data, key, sizeof key - 1) == 0)
        offset_value =
            ESP_TEXT(e, ((jb_json_span_t){line.data + sizeof key - 1,
                                          line.length - sizeof key + 1}));
  }
  jb_string_destroy(&text);
  if (!esp_ok(e))
    return false;
  if (active_count != 1)
    return ESP_FAIL(e, jh_span("sdkconfig must select exactly one supported "
                               "partition profile"));
  if (offset_value == NULL)
    return ESP_FAIL(e, jh_span("sdkconfig has no partition table offset"));
  uint64_t offset = 0;
  if (!jh_metadata_unsigned(jh_span(offset_value), &offset))
    return ESP_FAIL(
        e, jh_span("sdkconfig has an invalid partition table offset: "),
        esp_repr(e, jh_span(offset_value)));
  size_t found = SIZE_MAX, count = 0;
  for (size_t i = 0; i < jb_vector_size(paths); ++i)
    if (strcmp(esp_at(paths, i), "partition_table/partition-table.bin") == 0) {
      found = i;
      ++count;
    }
  if (count != 1)
    return ESP_FAIL(
        e, jh_span("Flash manifest has no unique partition table image"));
  const char *expected = esp_hex(e, offset);
  if (strcmp(esp_at(offsets, found), expected) != 0)
    return ESP_FAIL(e,
                    jh_span("Partition table flash offset differs from "
                            "sdkconfig: expected "),
                    jh_span(expected), jh_span(", found "),
                    esp_repr(e, jh_span(esp_at(offsets, found))));
  /* removeprefix("CONFIG_PARTITION_TABLE_").lower().replace("_", "-") */
  jb_string_t name = {0};
  if (esp_storage(e, jb_string_init(&name, jb_heap_allocator())) &&
      esp_storage(e,
                  jh_append(&name, active + strlen("CONFIG_PARTITION_TABLE_"))))
    for (char *c = jb_string_mutable_data(&name); *c != '\0'; ++c)
      *c = *c == '_'                  ? '-'
           : (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a')
                                      : *c;
  *profile = ESP_TEXT(e, jh_string_span(&name));
  jb_string_destroy(&name);
  *partition = found;
  (void)digests;
  return esp_ok(e);
}

bool esp_idf_environment(jh_esp_t *e, const jb_vector_t *environment,
                         jb_vector_t *out) {
  if (environment == NULL) {
    if (!esp_environment_capture(e, out))
      return false;
  } else {
    if (!esp_list_init(e, out))
      return false;
    for (size_t i = 0; i < jb_vector_size(environment) && esp_ok(e); ++i)
      (void)esp_push(e, out, jh_string_span(jh_list_at(environment, i)));
  }
  const char *generated = esp_join(e, e->build_dir, "generated");
  generated = esp_join(e, generated, "jaszczurhal");
  return esp_environment_set(e, out, "JH_ESP_IDF_GENERATED_DIR", generated) &&
         esp_environment_set(
             e, out, "JH_ESP_IDF_PROJECT_CONFIG",
             esp_join(e, generated, "jh_esp_idf_project.cmake"));
}

/* Run ninja in the build directory, its stdout and stderr merged. */
static hal_status_t run_ninja(jh_esp_t *e, const char *ninja,
                              const jb_vector_t *arguments, jb_string_t *out,
                              int *code) {
  jb_vector_t environment = {0};
  hal_status_t status = HAL_EINVAL;
  if (esp_idf_environment(e, e->exported ? &e->environment : NULL,
                          &environment)) {
    jb_string_t raw = {0};
    if (esp_storage(e, jb_string_init(&raw, jb_heap_allocator()))) {
      const esp_process_t process = {
          ninja, arguments, e->build_dir, &environment, ESP_RUN_MERGE,
          false, true,      30000,        &raw,         NULL,
          0};
      status = esp_process(e, &process, code);
      if (status == HAL_OK)
        (void)esp_decode_text(e, jh_string_span(&raw), out);
    }
    jb_string_destroy(&raw);
  }
  jh_list_destroy(&environment);
  return status;
}

/* validate_ninja_freshness */
static bool ninja_freshness(jh_esp_t *e, const jb_file_data_t *cache,
                            jb_json_tree_view_t provenance) {
  if (strcmp(cache_value(e, cache, "CMAKE_GENERATOR"), "Ninja") != 0)
    return esp_ok(e) &&
           ESP_FAIL(e, jh_span("ESP-IDF artifact freshness requires Ninja"));
  const char *ninja = cache_value(e, cache, "CMAKE_MAKE_PROGRAM");
  const char *name = esp_name(ninja);
  const jb_json_tree_view_t expected = jh_json_field(provenance, "ninja");
  jb_json_span_t expected_name = {0};
  jb_string_t lowered = {0};
  bool known = false;
  if (esp_storage(e, jb_string_init(&lowered, jb_heap_allocator())) &&
      esp_storage(e, jh_append(&lowered, name))) {
    for (char *c = jb_string_mutable_data(&lowered); *c != '\0'; ++c)
      if (*c >= 'A' && *c <= 'Z')
        *c = (char)(*c - 'A' + 'a');
    known = strcmp(jb_string_data(&lowered), "ninja") == 0 ||
            strcmp(jb_string_data(&lowered), "ninja.exe") == 0 ||
            strcmp(jb_string_data(&lowered), "ninja-build") == 0;
  }
  jb_string_destroy(&lowered);
  if (esp_ok(e) &&
      (!esp_absolute(ninja) || !esp_is_file(ninja) || !known ||
       jh_json_type(expected) != JB_JSON_OBJECT ||
       !jh_json_string(jh_json_field(expected, "name"), &expected_name) ||
       !jh_span_equal(expected_name, jh_span(name))))
    return ESP_FAIL(e, jh_span("CMake cache does not identify the provenanced "
                               "Ninja executable"));
  jb_vector_t arguments = {0};
  jb_string_t output = {0};
  int code = -1;
  if (!esp_list_init(e, &arguments) ||
      !esp_storage(e, jb_string_init(&output, jb_heap_allocator())) ||
      !esp_push(e, &arguments, jh_span("-n")) ||
      !esp_push(e, &arguments,
                jh_span(ESP_TEXT(e, jh_span(e->model.project_name),
                                 jh_span(".elf")))))
    goto done;
  const hal_status_t status = run_ninja(e, ninja, &arguments, &output, &code);
  if (!esp_ok(e))
    goto done;
  if (status != HAL_OK) {
    (void)ESP_FAIL(e, jh_span("Cannot check Ninja artifact freshness: "),
                   jh_span(hal_status_to_string(status)));
    goto done;
  }
  const jb_json_span_t stripped = esp_strip(jh_string_span(&output));
  if (code != 0) {
    char number[16];
    (void)snprintf(number, sizeof number, "%d", code);
    (void)ESP_FAIL(e,
                   jh_span("Ninja artifact freshness check failed with exit "),
                   jh_span(number), jh_span(": "), stripped);
    goto done;
  }
  if (jh_span_equal(stripped, jh_span("ninja: no work to do.")))
    goto done;
  jb_vector_t preview = {0}, newer = {0};
  if (esp_list_init(e, &preview) && esp_list_init(e, &newer)) {
    jb_json_span_t input = stripped, line = {0};
    while (jb_vector_size(&preview) < 8 && jh_metadata_next_line(&input, &line))
      (void)esp_push(e, &preview, line);
    jb_vector_t explain = {0};
    jb_string_t explained = {0};
    int explain_code = -1;
    if (esp_list_init(e, &explain) &&
        esp_storage(e, jb_string_init(&explained, jb_heap_allocator())) &&
        esp_push(e, &explain, jh_span("-d")) &&
        esp_push(e, &explain, jh_span("explain")) &&
        esp_push(e, &explain, jh_span("-n")) &&
        esp_push(e, &explain, jh_string_span(jh_list_at(&arguments, 1))) &&
        run_ninja(e, ninja, &explain, &explained, &explain_code) == HAL_OK) {
      jb_json_span_t rest = jh_string_span(&explained);
      while (jh_metadata_next_line(&rest, &line) && esp_ok(e)) {
        jb_json_span_t group = {0};
        if (esp_search(e, "older than most recent input (\\S+)", line, false,
                       &group, NULL))
          (void)esp_push_unique(e, &newer, group);
      }
    }
    jb_string_destroy(&explained);
    jh_list_destroy(&explain);
    const char *text = esp_join_list(e, &preview, "\n");
    if (jb_vector_size(&newer) != 0) {
      jb_vector_t shown = {0};
      if (esp_list_init(e, &shown))
        for (size_t i = 0; i < jb_vector_size(&newer) && i < 5; ++i)
          (void)esp_push(
              e, &shown,
              jh_span(ESP_TEXT(e, jh_span("  "),
                               jh_string_span(jh_list_at(&newer, i)))));
      text = ESP_TEXT(
          e, jh_span(text),
          jh_span("\nInputs newer than the artifacts they feed; an edit "
                  "landed while the build ran:\n"),
          jh_span(esp_join_list(e, &shown, "\n")));
      jh_list_destroy(&shown);
    }
    (void)ESP_FAIL(e,
                   jh_span("ESP-IDF artifacts are stale; Ninja reports pending "
                           "work:\n"),
                   jh_span(text));
  }
  jh_list_destroy(&newer);
  jh_list_destroy(&preview);
done:
  jb_string_destroy(&output);
  jh_list_destroy(&arguments);
  return esp_ok(e);
}

/* A model value against the resolved board record: a string or a list of
 * strings (sorted on both sides for a device tree build). */
static bool check_resolved(jh_esp_t *e, const char *key,
                           jb_json_tree_view_t actual,
                           const char *expected_text,
                           const jb_vector_t *expected_list, bool sort) {
  bool same = false;
  jb_json_span_t expected_repr = {0};
  if (expected_list == NULL) {
    jb_json_span_t text = {0};
    same = actual.tree != NULL && jh_json_string(actual, &text) &&
           jh_span_equal(text, jh_span(expected_text));
    expected_repr = esp_repr(e, jh_span(expected_text));
  } else {
    jb_vector_t a = {0}, b = {0};
    if (esp_list_init(e, &a) && esp_list_init(e, &b)) {
      /* sorted(actual or []) of a device tree build. */
      const bool none =
          actual.tree == NULL || jh_json_type(actual) == JB_JSON_NULL;
      bool strings = none ? sort : jh_json_type(actual) == JB_JSON_ARRAY;
      for (size_t i = 0; strings && !none && i < jh_json_count(actual); ++i) {
        jb_json_span_t text = {0};
        strings = jh_json_string(jh_json_item(actual, i), &text) &&
                  esp_push(e, &a, text);
      }
      for (size_t i = 0; i < jb_vector_size(expected_list); ++i)
        (void)esp_push(e, &b, jh_string_span(jh_list_at(expected_list, i)));
      if (sort) {
        jh_list_sort(&a);
        jh_list_sort(&b);
      }
      same = strings && jb_vector_size(&a) == jb_vector_size(&b);
      for (size_t i = 0; same && i < jb_vector_size(&a); ++i)
        same = strcmp(esp_at(&a, i), esp_at(&b, i)) == 0;
      expected_repr = esp_repr_list(e, &b);
    }
    jh_list_destroy(&b);
    jh_list_destroy(&a);
  }
  if (esp_ok(e) && !same)
    (void)ESP_FAIL(e, jh_span("Resolved board config "), jh_span(key),
                   jh_span(" mismatch: expected "), expected_repr,
                   jh_span(", found "), esp_repr_tree(e, actual));
  return esp_ok(e);
}

/* _missing_compiled_sources */
static bool compiled_sources(jh_esp_t *e, jb_json_tree_view_t entries) {
  jb_vector_t paths = {0}, names = {0}, required = {0}, missing = {0};
  if (!esp_list_init(e, &paths) || !esp_list_init(e, &names) ||
      !esp_list_init(e, &required) || !esp_list_init(e, &missing))
    goto done;
  for (size_t i = 0; i < jh_json_count(entries) && esp_ok(e); ++i) {
    const jb_json_tree_view_t entry = jh_json_item(entries, i);
    if (jh_json_type(entry) != JB_JSON_OBJECT)
      continue;
    jb_json_span_t file = {"", 0};
    jb_json_tree_view_t value = {0};
    if (jh_json_member(entry, "file", &value) && !jh_json_string(value, &file))
      continue;
    const char *name = ESP_TEXT(e, file);
    (void)esp_push(e, &paths, jh_span(esp_resolve(e, name, e->options->base)));
    jb_string_t normalized = {0};
    if (esp_storage(e, jb_string_init(&normalized, jb_heap_allocator())) &&
        esp_storage(e, jh_append(&normalized, name)))
      for (char *c = jb_string_mutable_data(&normalized); *c != '\0'; ++c)
        if (*c == '\\')
          *c = '/';
    (void)esp_push(e, &names, jh_string_span(&normalized));
    jb_string_destroy(&normalized);
  }
  const jh_esp_model_t *m = &e->model;
  for (size_t i = 0; i < jb_vector_size(&m->project_sources) && esp_ok(e);
       ++i) {
    const char *source = esp_at(&m->project_sources, i);
    if (!jh_list_contains(&paths,
                          jh_span(esp_resolve(e, source, e->options->base))))
      (void)esp_push(e, &missing, jh_span(source));
  }
  for (size_t i = 0; i < jb_vector_size(&m->integration_sources) && esp_ok(e);
       ++i) {
    const char *source =
        esp_join(e, e->repo_root, esp_at(&m->integration_sources, i));
    if (!jh_list_contains(&paths,
                          jh_span(esp_resolve(e, source, e->options->base))))
      (void)esp_push(e, &missing, jh_span(source));
  }
  static const char *const contract[] = {
      "/" JH_ESP_GENERATED_DIR "/jh_link_contract_reference.c",
      "/" JH_ESP_GENERATED_DIR "/jh_link_contract_definition.c"};
  for (size_t c = 0; c < COUNTOF(contract) && esp_ok(e); ++c) {
    bool found = false;
    const size_t n = strlen(contract[c]);
    for (size_t i = 0; i < jb_vector_size(&names) && !found; ++i) {
      const char *name = esp_at(&names, i);
      const size_t length = strlen(name);
      found = length >= n && strcmp(name + length - n, contract[c]) == 0;
    }
    if (!found)
      (void)esp_push(e, &missing, jh_span(contract[c]));
  }
  if (esp_ok(e) && jb_vector_size(&missing) != 0)
    (void)ESP_FAIL(e,
                   jh_span("compile_commands.json is missing integration "
                           "sources: "),
                   jh_span(esp_join_list(e, &missing, ", ")));
done:
  jh_list_destroy(&missing);
  jh_list_destroy(&required);
  jh_list_destroy(&names);
  jh_list_destroy(&paths);
  return esp_ok(e);
}

typedef struct {
  uint64_t offset;
  size_t index;
} image_t;

static int compare_images(const void *left, const void *right) {
  const image_t *a = left, *b = right;
  if (a->offset != b->offset)
    return a->offset < b->offset ? -1 : 1;
  return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

bool esp_validate_artifacts(jh_esp_t *e, size_t *flash_images) {
  const jh_esp_model_t *m = &e->model;
  const char *keys[] = {"applicationElf",
                        "applicationMap",
                        "applicationBinary",
                        "sdkconfig",
                        "flasherArgs",
                        "projectDescription",
                        "compileCommands",
                        "cmakeCache",
                        "buildLog",
                        "generatedConfig",
                        "generatedLinkContract",
                        "resolvedBoardConfig",
                        "projectConfig",
                        "toolchainProvenance"};
  const char *names[COUNTOF(keys)] = {
      ESP_TEXT(e, jh_span(m->project_name), jh_span(".elf")),
      ESP_TEXT(e, jh_span(m->project_name), jh_span(".map")),
      ESP_TEXT(e, jh_span(m->project_name), jh_span(".bin")),
      "sdkconfig",
      "flasher_args.json",
      "project_description.json",
      "compile_commands.json",
      "CMakeCache.txt",
      "build.log",
      JH_ESP_GENERATED_DIR "/jh_board_config.h",
      JH_ESP_GENERATED_DIR "/jh_link_contract.h",
      m->device_tree ? JH_ESP_GENERATED_DIR "/jh_hardware_config.json"
                     : JH_ESP_GENERATED_DIR "/jh_board_resolved.json",
      JH_ESP_GENERATED_DIR "/jh_esp_idf_project.json",
      JH_ESP_GENERATED_DIR "/" TOOLCHAIN_JSON};
  const char *paths[COUNTOF(keys)] = {0}, *relatives[COUNTOF(keys)] = {0};
  for (size_t i = 0; i < COUNTOF(keys) && esp_ok(e); ++i)
    (void)artifact_path(e, names[i], &paths[i], &relatives[i]);
  jb_json_tree_t *project_tree = NULL, *provenance_tree = NULL,
                 *description_tree = NULL, *commands_tree = NULL,
                 *resolved_tree = NULL, *flasher_tree = NULL,
                 *final_tree = NULL;
  jb_json_tree_view_t project = {0}, provenance = {0}, description = {0},
                      commands = {0}, record = {0}, flasher = {0};
  contract_t c;
  contract_init(&c);
  jb_file_data_t cache = {0};
  jb_vector_t offsets = {0}, image_paths = {0}, sizes = {0}, digests = {0};
  image_t *images = NULL;
  esp_json_t json = {0};
  if (!esp_ok(e) || !esp_validate_board_contract(e))
    goto done;
  project_tree = esp_validate_project_contract(e, &project);
  if (project_tree == NULL || !clean_log(e, paths[8]))
    goto done;
  provenance_tree = esp_load_json(e, paths[13], true, &provenance);
  if (provenance_tree == NULL || !load_contract(e, &c) ||
      !validate_toolchain(e, provenance, &c))
    goto done;
  description_tree = esp_load_json(e, paths[5], true, &description);
  if (description_tree == NULL)
    goto done;
  const struct {
    const char *key, *expected, *label;
  } description_checks[] = {
      {"target", m->idf_target, "project_description.json target mismatch: "},
      {"project_name", m->project_name,
       "project_description.json project mismatch: "},
      {"app_elf", names[0], "project_description.json app_elf mismatch: "},
      {"app_bin", names[2], "project_description.json app_bin mismatch: "}};
  for (size_t i = 0; i < COUNTOF(description_checks) && esp_ok(e); ++i) {
    jb_json_tree_view_t value = {0};
    jb_json_span_t text = {0};
    const bool has =
        jh_json_member(description, description_checks[i].key, &value);
    if (has && jh_json_string(value, &text) &&
        jh_span_equal(text, jh_span(description_checks[i].expected)))
      continue;
    (void)ESP_FAIL(
        e, jh_span(description_checks[i].label), jh_span("expected "),
        esp_repr(e, jh_span(description_checks[i].expected)),
        jh_span(", found "), has ? esp_repr_tree(e, value) : jh_span("None"));
  }
  const jb_json_tree_view_t components =
      jh_json_field(description, "build_components");
  bool component = false;
  for (size_t i = 0; esp_ok(e) && jh_json_type(components) == JB_JSON_ARRAY &&
                     i < jh_json_count(components) && !component;
       ++i) {
    jb_json_span_t text = {0};
    if (!jh_json_string(jh_json_item(components, i), &text))
      continue;
    static const char wanted[] = "jaszczurhal";
    component = text.length == sizeof wanted - 1;
    for (size_t k = 0; component && k < text.length; ++k) {
      char ch = text.data[k];
      if (ch >= 'A' && ch <= 'Z')
        ch = (char)(ch - 'A' + 'a');
      component = ch == wanted[k];
    }
  }
  if (esp_ok(e) && !component) {
    (void)ESP_FAIL(e, jh_span("project_description.json does not include the "
                              "jaszczurhal component"));
    goto done;
  }
  if (!esp_ok(e))
    goto done;
  {
    jh_json_location_t where;
    jh_json_location_init(&where);
    jh_failure_t error;
    jh_failure_init(&error);
    if (jh_json_read_value(&where, paths[6], JH_ESP_MAX_BYTES, &commands_tree,
                           &error) != HAL_OK)
      (void)ESP_FAIL(e, jh_span("Invalid compile_commands.json: "),
                     jh_string_span(&error.message));
    else
      (void)jb_json_tree_root(commands_tree, &commands);
    jh_failure_destroy(&error);
    jh_json_location_destroy(&where);
  }
  if (!esp_ok(e))
    goto done;
  if (jh_json_type(commands) != JB_JSON_ARRAY || jh_json_count(commands) == 0) {
    (void)ESP_FAIL(e,
                   jh_span("compile_commands.json must contain build entries"));
    goto done;
  }
  if (!compiled_sources(e, commands))
    goto done;
  resolved_tree = esp_load_json(e, paths[11], true, &record);
  if (resolved_tree == NULL)
    goto done;
  jb_json_tree_view_t feature_hash = {0}, requested = {0}, resolved = {0};
  const char *tree_hash = "";
  if (!m->device_tree) {
    feature_hash = jh_json_field(record, "featureHash");
    requested = jh_json_field(record, "requestedFeatures");
    resolved = jh_json_field(record, "resolvedFeatures");
    (void)(check_resolved(e, "target", jh_json_field(record, "target"),
                          m->target, NULL, false) &&
           check_resolved(e, "board", jh_json_field(record, "board"), m->board,
                          NULL, false) &&
           check_resolved(e, "idfTarget", jh_json_field(record, "idfTarget"),
                          m->idf_target, NULL, false) &&
           check_resolved(e, "requestedFeatures", requested, NULL,
                          &m->requested, false) &&
           check_resolved(e, "resolvedFeatures", resolved, NULL, &m->resolved,
                          false));
  } else {
    const jb_json_tree_view_t selection = jh_json_field(record, "selection");
    const jb_json_tree_view_t software = jh_json_field(record, "software");
    jb_json_span_t digest = {"", 0};
    (void)jh_json_string(jh_json_field(record, "configSha256"), &digest);
    if (digest.length > 12)
      digest.length = 12;
    tree_hash = ESP_TEXT(e, digest);
    requested = jh_json_field(software, "requestedFeatures");
    resolved = jh_json_field(software, "resolvedFeatures");
    (void)(check_resolved(e, "target", jh_json_field(selection, "target"),
                          m->target, NULL, false) &&
           check_resolved(e, "board", jh_json_field(selection, "board"),
                          m->board, NULL, false) &&
           check_resolved(e, "requestedFeatures", requested, NULL,
                          &m->requested, true) &&
           check_resolved(e, "resolvedFeatures", resolved, NULL, &m->resolved,
                          true));
  }
  if (!esp_ok(e))
    goto done;
  flasher_tree = esp_load_json(e, paths[4], true, &flasher);
  if (flasher_tree == NULL)
    goto done;
  jb_json_span_t chip = {0};
  const jb_json_tree_view_t extra =
      jh_json_field(flasher, "extra_esptool_args");
  if (jh_json_type(extra) != JB_JSON_OBJECT ||
      !jh_json_string(jh_json_field(extra, "chip"), &chip) ||
      !jh_span_equal(chip, jh_span(m->idf_target))) {
    (void)ESP_FAIL(
        e, jh_span("flasher_args.json does not select the exact target"));
    goto done;
  }
  int64_t flash_bytes = 0;
  (void)jh_json_int64(
      jh_json_field(
          jh_json_field(jh_json_field(m->board_descriptor->root, "memory"),
                        "flash"),
          "expectedBytes"),
      &flash_bytes);
  char flash_size[32];
  (void)snprintf(flash_size, sizeof flash_size, "%lldMB",
                 (long long)(flash_bytes / (1024 * 1024)));
  jb_json_span_t size_text = {0};
  const jb_json_tree_view_t settings = jh_json_field(flasher, "flash_settings");
  if (jh_json_type(settings) != JB_JSON_OBJECT ||
      !jh_json_string(jh_json_field(settings, "flash_size"), &size_text) ||
      !jh_span_equal(size_text, jh_span(flash_size))) {
    (void)ESP_FAIL(
        e, jh_span("flasher_args.json flash size differs from the board "
                   "descriptor"));
    goto done;
  }
  const jb_json_tree_view_t files = jh_json_field(flasher, "flash_files");
  if (jh_json_type(files) != JB_JSON_OBJECT || jh_json_count(files) == 0) {
    (void)ESP_FAIL(e, jh_span("flasher_args.json has no flash_files object"));
    goto done;
  }
  const size_t count = jh_json_count(files);
  images = calloc(count, sizeof *images);
  if (images == NULL || !esp_list_init(e, &offsets) ||
      !esp_list_init(e, &image_paths) || !esp_list_init(e, &sizes) ||
      !esp_list_init(e, &digests)) {
    (void)esp_storage(e, HAL_ENOMEM);
    goto done;
  }
  for (size_t i = 0; i < count && esp_ok(e); ++i) {
    const jb_json_tree_view_t member = jh_json_item(files, i);
    const jb_json_span_t raw_offset = jh_json_key(member);
    jb_json_span_t raw_path = {0};
    if (!jh_json_string(member, &raw_path)) {
      (void)ESP_FAIL(e,
                     jh_span("flasher_args.json flash_files must map strings"));
      break;
    }
    uint64_t offset = 0;
    if (!jh_metadata_unsigned(raw_offset, &offset)) {
      (void)ESP_FAIL(e, jh_span("Invalid flash offset: "),
                     esp_repr(e, raw_offset));
      break;
    }
    bool duplicate = false;
    for (size_t j = 0; j < i; ++j)
      duplicate = duplicate || images[j].offset == offset;
    if (duplicate) {
      (void)ESP_FAIL(e, jh_span("Invalid or duplicate flash offset: "),
                     esp_repr(e, raw_offset));
      break;
    }
    const char *path = "", *relative = "";
    if (!artifact_path(e, ESP_TEXT(e, raw_path), &path, &relative))
      break;
    if (jh_list_contains(&image_paths, jh_span(relative))) {
      (void)ESP_FAIL(e, jh_span("Duplicate flash image: "), jh_span(relative));
      break;
    }
    uint64_t size = 0;
    char digest[65] = {0};
    (void)esp_file_size(path, &size);
    if (!esp_sha256_file(e, path, digest))
      break;
    images[i] = (image_t){offset, i};
    (void)esp_push(e, &offsets, jh_span(esp_hex(e, offset)));
    (void)esp_push(e, &image_paths, jh_span(relative));
    (void)esp_push(e, &sizes, jh_span(esp_decimal(e, size)));
    (void)esp_push(e, &digests, jh_span(digest));
  }
  if (!esp_ok(e))
    goto done;
  {
    jb_vector_t missing = {0};
    const char *required[] = {names[2], "bootloader/bootloader.bin",
                              "partition_table/partition-table.bin"};
    if (esp_list_init(e, &missing)) {
      for (size_t i = 0; i < COUNTOF(required); ++i)
        if (!jh_list_contains(&image_paths, jh_span(required[i])))
          (void)esp_push(e, &missing, jh_span(required[i]));
      jh_list_sort(&missing);
      if (jb_vector_size(&missing) != 0)
        (void)ESP_FAIL(
            e, jh_span("flasher_args.json is missing required images: "),
            jh_span(esp_join_list(e, &missing, ", ")));
    }
    jh_list_destroy(&missing);
  }
  if (!esp_ok(e))
    goto done;
  qsort(images, count, sizeof *images, compare_images);
  jb_vector_t sorted_offsets = {0}, sorted_paths = {0};
  char sdkconfig[65] = {0};
  size_t partition = 0;
  const char *profile = "";
  if (esp_list_init(e, &sorted_offsets) && esp_list_init(e, &sorted_paths)) {
    for (size_t i = 0; i < count; ++i) {
      (void)esp_push(e, &sorted_offsets,
                     jh_string_span(jh_list_at(&offsets, images[i].index)));
      (void)esp_push(e, &sorted_paths,
                     jh_string_span(jh_list_at(&image_paths, images[i].index)));
    }
    (void)partition_configuration(e, paths[3], &sorted_offsets, &sorted_paths,
                                  &digests, sdkconfig, &partition, &profile);
  }
  jh_list_destroy(&sorted_paths);
  jh_list_destroy(&sorted_offsets);
  if (!esp_ok(e) || !cmake_cache(e, &cache) ||
      !ninja_freshness(e, &cache, provenance))
    goto done;
  const size_t table = images[partition].index;
  if (!esp_json_begin(e, &json))
    goto done;
  esp_json_u64(&json, "schemaVersion", 1);
  esp_json_string(&json, "project", jh_span(m->project_name));
  esp_json_string(&json, "target", jh_span(m->target));
  esp_json_string(&json, "board", jh_span(m->board));
  /* A device tree build also names the assembly that fixed both. */
  if (m->device_tree)
    esp_json_string(&json, "assembly", jh_span(m->assembly));
  esp_json_object(&json, "espIdf");
  esp_json_string(&json, "version", jh_span(e->idf_version));
  esp_json_string(&json, "commit", jh_span(e->idf_commit));
  esp_json_object_end(&json);
  esp_json_tree(&json, "toolchain", provenance);
  esp_json_object(&json, "configuration");
  esp_json_string(&json, "sdkconfigSha256", jh_span(sdkconfig));
  esp_json_object(&json, "partitionTable");
  esp_json_string(&json, "profile", jh_span(profile));
  esp_json_string(&json, "path",
                  jh_string_span(jh_list_at(&image_paths, table)));
  esp_json_string(&json, "offset", jh_string_span(jh_list_at(&offsets, table)));
  esp_json_string(&json, "sha256", jh_string_span(jh_list_at(&digests, table)));
  esp_json_object_end(&json);
  esp_json_object_end(&json);
  esp_json_object(&json, "integration");
  esp_json_string(&json, "component", jh_span("jaszczurhal"));
  esp_json_list(&json, "sources", &m->integration_sources);
  esp_json_tree(&json, "projectSources",
                jh_json_field(project, "projectSources"));
  esp_json_tree(&json, "projectLinkLibraries",
                jh_json_field(project, "projectLinkLibraries"));
  if (m->device_tree)
    esp_json_string(&json, "featureHash", jh_span(tree_hash));
  else
    esp_json_tree(&json, "featureHash", feature_hash);
  esp_json_tree(&json, "requestedFeatures", requested);
  esp_json_tree(&json, "resolvedFeatures", resolved);
  esp_json_object_end(&json);
  esp_json_array(&json, "flashImages");
  for (size_t i = 0; i < count; ++i) {
    const size_t index = images[i].index;
    esp_json_object(&json, NULL);
    esp_json_string(&json, "offset",
                    jh_string_span(jh_list_at(&offsets, index)));
    esp_json_string(&json, "path",
                    jh_string_span(jh_list_at(&image_paths, index)));
    esp_json_key(&json, "size");
    if (json.status == HAL_OK)
      json.status = jb_json_writer_number(
          &json.writer, jh_string_span(jh_list_at(&sizes, index)));
    esp_json_string(&json, "sha256",
                    jh_string_span(jh_list_at(&digests, index)));
    esp_json_object_end(&json);
  }
  esp_json_array_end(&json);
  esp_json_object(&json, "artifacts");
  for (size_t i = 0; i < COUNTOF(keys); ++i)
    esp_json_string(&json, keys[i], jh_span(relatives[i]));
  esp_json_object_end(&json);
  esp_json_end(&json);
  if (esp_storage(e, json.status))
    (void)esp_write_text(e, esp_join(e, e->build_dir, MANIFEST_JSON),
                         jh_span(esp_dumps(e, jh_string_span(&json.text))));
  jb_string_destroy(&json.text);
  *flash_images = count;
done:
  free(images);
  jh_list_destroy(&digests);
  jh_list_destroy(&sizes);
  jh_list_destroy(&image_paths);
  jh_list_destroy(&offsets);
  jb_file_release(&cache);
  contract_destroy(&c);
  jb_json_tree_destroy(final_tree);
  jb_json_tree_destroy(flasher_tree);
  jb_json_tree_destroy(resolved_tree);
  jb_json_tree_destroy(commands_tree);
  jb_json_tree_destroy(description_tree);
  jb_json_tree_destroy(provenance_tree);
  jb_json_tree_destroy(project_tree);
  return esp_ok(e);
}

// jh::read_settings on files of a private directory: the accepted model, a
// missing file, the root type, parse errors located in the original file,
// limits, the differences from Python and a path that is no file. A native
// test, so the gate also runs it under Valgrind and the sanitizers.
#include "jh_json_file.h"
#include "jh_stl_bridge.h"
#include <cstdio>
#include <string>

namespace {
int failures = 0;
void expect(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "settings: %s\n", what);
    ++failures;
  }
}

// The compact text of what read_settings returned, or its failure.
struct outcome {
  jh::owned_string text; // append_text appends to an owned_string.
  bool exists = false;
  jh::failure error{HAL_OK, 0, {}, {}, {}, {}};
  jh::parse_location where;
};

outcome read(const std::string &path, size_t limit = 65536) {
  outcome result;
  try {
    const auto file = jh::read_settings(result.where, path.c_str(), limit);
    result.exists = file.exists;
    jb_json_tree_view_t root;
    jh::require(jb_json_tree_root(file.tree.get(), &root), 2, "no root");
    const jb_json_tree_format_t format{limit * 6, 0, false, false};
    size_t written = 0;
    jh::require(jb_json_tree_format(root, &format, jh::append_text,
                                    &result.text, &written),
                4, "cannot format the tree");
  } catch (const jh::failure &error) {
    result.error = error;
  }
  return result;
}

std::string write(const std::string &directory, const char *name,
                  const std::string &bytes) {
  const std::string path = jh::stl::absolute_path(name, directory);
  bool changed = false;
  expect(jb_file_write_atomic(path.c_str(), bytes.data(), bytes.size(), false,
                              &changed) == HAL_OK,
         name);
  return path;
}

bool failed(const outcome &result, hal_status_t status, int exit_code) {
  return result.error.status == status && result.error.exit_code == exit_code;
}

void accepted_model(const std::string &directory) {
  const auto result = read(
      write(directory, "settings.json",
            "{\"a\": 1, // line\r\n \"a\": [NaN, -Infinity, 1.50e3, -0],\r"
            " /* block\n */ \"b\": \"\\u017c\xc5\xbc\", \"c\": {\"d\": true, "
            "\"d\": null}}"));
  expect(result.error.status == HAL_OK && result.exists, "settings accepted");
  expect(result.text == "{\"a\":[NaN,-Infinity,1.50e3,-0],"
                        "\"b\":\"\xc5\xbc\xc5\xbc\",\"c\":{\"d\":null}}",
         "comments dropped, last duplicate kept in place, tokens kept");
}

void missing_file(const std::string &directory) {
  const auto result = read(jh::stl::absolute_path("missing.json", directory));
  expect(result.error.status == HAL_OK && !result.exists && result.text == "{}",
         "a missing file reads as an empty object");
}

void root_type(const std::string &directory) {
  const std::string path = write(directory, "array.json", "[1]");
  const auto result = read(path);
  expect(failed(result, HAL_ETYPE, 2) &&
             result.error.message == path + " must contain a JSON object" &&
             !result.where.located,
         "a root other than an object");
}

void located_error(const std::string &directory) {
  const auto result =
      read(write(directory, "invalid.json",
                 "{\n  // \"a\": 1,\n  /* two\n     lines */ \"b\": }\n"));
  expect(failed(result, HAL_EPROTO, 2) && result.where.located &&
             result.where.line == 4 && result.where.column == 20,
         "a parse error is located in the file with its comments");
}

void limits(const std::string &directory) {
  const std::string deep =
      "{\"a\": " + std::string(256, '[') + std::string(256, ']') + "}";
  expect(failed(read(write(directory, "deep.json", deep)), HAL_EOVERFLOW, 4),
         "257 nested containers exceed the parser limit");
  const std::string path = write(directory, "large.json", "{\"a\": \"long\"}");
  expect(read(path, 8).error.exit_code == 4, "a file above the limit");
}

void python_differences(const std::string &directory) {
  expect(
      failed(read(write(directory, "surrogate.json", "{\"a\": \"\\ud800\"}")),
             HAL_EPROTO, 2),
      "an unpaired surrogate escape is rejected");
  const std::string digits(4301, '7');
  const auto result =
      read(write(directory, "integer.json", "{\"a\": " + digits + "}"));
  expect(result.error.status == HAL_OK &&
             result.text == "{\"a\":" + digits + "}",
         "an integer above the int() limit of Python keeps its token");
}

void not_a_file(const std::string &directory) {
  expect(read(directory).error.exit_code == 3, "a directory is not a file");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2)
    return 64;
  jb_temp_dir_t *owned = nullptr;
  if (jb_temp_dir_create(argv[1], &owned) != HAL_OK) {
    std::fprintf(stderr, "settings: cannot create a directory in %s\n",
                 argv[1]);
    return 1;
  }
  const std::string directory = jb_temp_dir_path(owned);
  accepted_model(directory);
  missing_file(directory);
  root_type(directory);
  located_error(directory);
  limits(directory);
  python_differences(directory);
  not_a_file(directory);
  expect(jb_temp_dir_remove(&owned) == HAL_OK, "remove the directory");
  if (failures == 0)
    std::printf("settings: all checks passed\n");
  return failures == 0 ? 0 : 1;
}

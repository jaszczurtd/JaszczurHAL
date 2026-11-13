/* Real CLI paths with failed filesystem/configuration calls. The separately
 * compiled command objects redirect only these calls, on Unix and MSVC alike.
 */
#define main tested_main
#define wmain tested_wmain
#include "../src/main.c"
#undef main
#undef wmain
#include "jh_project_config.h"
#include <jb/filesystem.h>

static enum { no_fault, read_fault, mkdir_fault, stat_fault } fault;
static hal_status_t stat_error;
static bool stat_follow, stat_link;
static unsigned hits, failures;

hal_status_t jh_test_fs_status(const char *path, bool follow,
                               jb_fs_info_t *info) {
  if (fault == stat_fault && jh_span_equal(jh_span_path_stem(jh_span(path)),
                                           jh_span("fault-dependency"))) {
    if (follow == stat_follow) {
      ++hits;
      return stat_error;
    }
    /* Model a link on both hosts without requiring Windows symlink rights. */
    if (stat_link && !follow) {
      const hal_status_t status = jb_fs_status(path, false, info);
      if (status == HAL_OK)
        info->kind = JB_FS_LINK;
      return status;
    }
  }
  return jb_fs_status(path, follow, info);
}

hal_status_t jh_test_fs_mkdir(const char *path, bool parents) {
  if (fault == mkdir_fault) {
    ++hits;
    return HAL_ENOMEM;
  }
  return jb_fs_mkdir(path, parents);
}

hal_status_t jh_test_config_read(const char *header, const jb_vector_t *defines,
                                 const char *preinclude,
                                 const jh_config_limits_t *limits,
                                 jh_project_config_t *config,
                                 jh_failure_t *failure) {
  if (fault == read_fault) {
    ++hits;
    return jh_fail_coded(failure, HAL_ENOMEM, 4, "JH-CFG-SCOPE",
                         jh_span(header),
                         JH_PARTS(jh_span("cannot read header")));
  }
  return jh_project_config_read(header, defines, preinclude, limits, config,
                                failure);
}

static void expect(bool ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "CLI failure: %s\n", what);
    ++failures;
  }
}

static void expect_read_diagnostic(size_t argc, const char *const *argv) {
  jh_cli_t ctx = {0};
  jh_failure_init(&ctx.failure);
  jh_json_location_init(&ctx.location);
  arguments_init(&ctx.args);
  (void)jh_list_init(&ctx.strings);
  (void)jb_vector_init(&ctx.workspace, 1, jb_heap_allocator());
  (void)jb_string_init(&ctx.result, jb_heap_allocator());
  (void)dispatch(&ctx, argc, argv);
  expect(ctx.failure.status == HAL_ENOMEM && ctx.failure.exit_code == 4 &&
             strcmp(jb_string_data(&ctx.failure.code), "JH-CFG-SCOPE") == 0 &&
             strstr(jb_string_data(&ctx.failure.source),
                    "hal_project_config.h") != NULL &&
             strstr(jb_string_data(&ctx.failure.message),
                    "cannot read header") != NULL,
         "fatal lint read preserves message, code and source");
  jb_string_destroy(&ctx.result);
  jb_vector_destroy(&ctx.workspace);
  jh_list_destroy(&ctx.strings);
  arguments_destroy(&ctx.args);
  jh_json_location_destroy(&ctx.location);
  jh_failure_destroy(&ctx.failure);
}

static bool write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return false;
  const bool written = fputs(text, file) >= 0;
  return fclose(file) == 0 && written;
}

static hal_status_t count_file(void *user, const char *path,
                               jb_json_span_t relative, const char *name) {
  (void)path;
  (void)relative;
  (void)name;
  ++*(unsigned *)user;
  return HAL_OK;
}

int main(int argc, char **argv) {
  if (argc != 3)
    return 64;
  jb_string_t directory = {0}, header = {0}, output = {0}, dependency = {0};
  jb_string_init(&directory, jb_heap_allocator());
  jb_string_init(&header, jb_heap_allocator());
  jb_string_init(&output, jb_heap_allocator());
  jb_string_init(&dependency, jb_heap_allocator());
  if (jh_path_join(argv[2], "cli-failures", &directory) != HAL_OK ||
      jb_fs_mkdir(jb_string_data(&directory), true) != HAL_OK ||
      jh_path_join(jb_string_data(&directory), "hal_project_config.h",
                   &header) != HAL_OK ||
      jh_path_join(jb_string_data(&directory), "generated.cmake", &output) !=
          HAL_OK ||
      jh_path_join(jb_string_data(&directory), "fault-dependency.txt",
                   &dependency) != HAL_OK ||
      !write_text(jb_string_data(&header), "#define HAL_ENABLE_UNKNOWN 1\n") ||
      !write_text(jb_string_data(&dependency), "dependency\n")) {
    expect(false, "fixtures");
    goto done;
  }
  const char *lint[] = {"jh",
                        "feature",
                        "lint",
                        "--root",
                        argv[1],
                        "--input-root",
                        jb_string_data(&header),
                        "--report-only",
                        "--json"};
  expect(run(COUNTOF(lint), lint, true) == 0, "report-only control");
  fault = read_fault;
  hits = 0;
  expect(run(COUNTOF(lint), lint, true) == 4 && hits == 1,
         "report-only cannot accept an unread header after ENOMEM");
  expect_read_diagnostic(COUNTOF(lint), lint);
  fault = no_fault;
  expect(write_text(jb_string_data(&header),
                    "#define HAL_RP_CORE1_STACK_SIZE 2048\n"),
         "valid build header");
  const char *build[] = {"jh",
                         "config",
                         "cmake",
                         "--root",
                         argv[1],
                         "--target",
                         "rp2040",
                         "--config-dir",
                         jb_string_data(&directory),
                         "--output",
                         jb_string_data(&output),
                         "--json"};
  (void)jb_fs_remove(jb_string_data(&output), false);
  fault = mkdir_fault;
  hits = 0;
  expect(run(COUNTOF(build), build, true) == 4 && hits == 1,
         "mkdir ENOMEM is exit 4");
  fault = no_fault;
  jb_fs_info_t info;
  expect(jb_fs_status(jb_string_data(&output), false, &info) == HAL_ENOENT,
         "failed mkdir publishes no output");
  const char *hardware[] = {"jh",
                            "config",
                            "cmake",
                            "--root",
                            argv[1],
                            "--target",
                            "rp2040",
                            "--hardware-header",
                            jb_string_data(&dependency),
                            "--output",
                            jb_string_data(&output),
                            "--json"};
  fault = stat_fault;
  stat_follow = true;
  stat_error = HAL_ENOMEM;
  hits = 0;
  expect(run(COUNTOF(hardware), hardware, true) == 4 && hits == 1,
         "hardware header ENOMEM is not a missing file");

  jh_failure_t error;
  jh_failure_init(&error);
  for (unsigned follow = 0; follow < 2; ++follow) {
    const hal_status_t errors[] = {HAL_ENOMEM, HAL_EOVERFLOW, HAL_EIO};
    for (size_t i = 0; i < COUNTOF(errors); ++i) {
      jh_failure_destroy(&error);
      jh_failure_init(&error);
      fault = stat_fault;
      stat_follow = follow != 0;
      stat_link = stat_follow;
      stat_error = errors[i];
      hits = 0;
      unsigned visited = 0;
      const hal_status_t status =
          jh_walk_files(jb_string_data(&directory), jh_span(""), count_file,
                        &visited, &error);
      if (follow && errors[i] == HAL_EIO) {
        expect(status == HAL_OK && hits == 1 && visited == 1 &&
                   error.status == HAL_OK,
               "an unreadable link target is skipped");
      } else {
        expect(
            status == errors[i] && hits == 1 &&
                error.exit_code == (errors[i] == HAL_EIO ? 3 : 4),
            "dependency stat failure cannot publish an incomplete traversal");
        expect(strstr(jb_string_data(&error.message),
                      jb_string_data(&dependency)) != NULL,
               "dependency stat failure identifies the path");
      }
    }
  }
  fault = no_fault;
  unsigned visited = 0;
  expect(jh_walk_files(jb_string_data(&directory), jh_span(""), count_file,
                       &visited, &error) == HAL_OK &&
             visited == 2,
         "complete dependency traversal after the fault");
  jh_failure_destroy(&error);

  jh_cli_t context = {0};
  jh_failure_init(&context.failure);
  expect(!jh_cli_ok(&context, HAL_EINVAL) &&
             strcmp(failure_message(&context.failure), "operation failed") ==
                 0 &&
             report_failure(&context) == 3,
         "an unadorned helper error is not an allocation failure");
  jh_failure_destroy(&context.failure);
done:
  jb_string_destroy(&dependency);
  jb_string_destroy(&output);
  jb_string_destroy(&header);
  jb_string_destroy(&directory);
  return failures == 0 ? 0 : 1;
}

/* The C API of the library commands around their child processes:
 * jh_library_install runs `cmake --install` of a fixture build directory.
 * The cancellation callback receives the caller's own context, a true result
 * stops the child with exit code 130, and no callback runs it to the end.
 * Usage: jh_library_test <work dir> <cmake executable> */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "jh_json_file.h"
#include "jh_library_build.h"
#include <jb/filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;

static void expect(bool condition, const char *message) {
  if (!condition) {
    (void)fprintf(stderr, "library: %s\n", message);
    ++failures;
  }
}

typedef struct {
  unsigned calls, foreign;
  bool cancel;
} cancel_state_t;

static cancel_state_t *expected_state;

static bool canceled(void *context) {
  if (context != expected_state)
    ++expected_state->foreign;
  ++expected_state->calls;
  return expected_state->cancel;
}

/* A missing tree counts as removed. */
static bool remove_tree(const char *path) {
  const hal_status_t status = jb_fs_remove(path, true);
  return status == HAL_OK || status == HAL_ENOENT;
}

static bool write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return false;
  const bool written = fputs(text, file) >= 0;
  return fclose(file) == 0 && written;
}

/* jh_library_install finds cmake on PATH; put the one of this build first. */
static bool prepend_path(const char *cmake) {
  const char *slash = strrchr(cmake, '/');
#ifdef _WIN32
  const char *backslash = strrchr(cmake, '\\');
  if (backslash != NULL && (slash == NULL || backslash > slash))
    slash = backslash;
  const char separator = ';';
#else
  const char separator = ':';
#endif
  if (slash == NULL)
    return true;
  const char *old = getenv("PATH");
  jb_string_t path = {0};
  bool ok = jb_string_init(&path, jb_heap_allocator()) == HAL_OK &&
            jb_string_append(&path, cmake, (size_t)(slash - cmake)) == HAL_OK &&
            jb_string_append(&path, &separator, 1) == HAL_OK &&
            jb_string_append(&path, old != NULL ? old : "",
                             old != NULL ? strlen(old) : 0) == HAL_OK;
#ifdef _WIN32
  ok = ok && _putenv_s("PATH", jb_string_data(&path)) == 0;
#else
  ok = ok && setenv("PATH", jb_string_data(&path), 1) == 0;
#endif
  jb_string_destroy(&path);
  return ok;
}

/* One install of the fixture with callback and context; exit code of the
 * failure, 0 on success. */
static int install(const char *build, const char *prefix, const char *base,
                   jb_cancel_fn callback, void *context, hal_status_t *status) {
  jh_failure_t failure;
  jh_failure_init(&failure);
  *status = jh_library_install(build, prefix, base, true, callback, context,
                               &failure);
  const int code = failure.exit_code;
  jh_failure_destroy(&failure);
  return code;
}

int main(int argc, char **argv) {
  if (argc != 3)
    return 64;
  jb_string_t work = {0}, build = {0}, rules = {0}, prefix = {0}, archive = {0};
  (void)jb_string_init(&work, jb_heap_allocator());
  (void)jb_string_init(&build, jb_heap_allocator());
  (void)jb_string_init(&rules, jb_heap_allocator());
  (void)jb_string_init(&prefix, jb_heap_allocator());
  (void)jb_string_init(&archive, jb_heap_allocator());
  /* The install rules outlast a few polls of the callback, then write the
   * archive the install requires. */
  static const char script[] =
      "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E sleep 0.5)\n"
      "file(WRITE \"${CMAKE_INSTALL_PREFIX}/lib/libJaszczurHAL.a\" "
      "\"!<arch>\\n\")\n";
  if (!prepend_path(argv[2]) ||
      jh_path_join(argv[1], "library-api", &work) != HAL_OK ||
      !remove_tree(jb_string_data(&work)) ||
      jh_path_join(jb_string_data(&work), "build", &build) != HAL_OK ||
      jb_fs_mkdir(jb_string_data(&build), true) != HAL_OK ||
      jh_path_join(jb_string_data(&build), "cmake_install.cmake", &rules) !=
          HAL_OK ||
      !write_text(jb_string_data(&rules), script) ||
      jh_path_join(jb_string_data(&work), "prefix", &prefix) != HAL_OK ||
      jh_path_join(jb_string_data(&prefix), "lib/libJaszczurHAL.a", &archive) !=
          HAL_OK) {
    expect(false, "fixtures");
    goto done;
  }
  const char *b = jb_string_data(&build), *p = jb_string_data(&prefix);
  const char *base = jb_string_data(&work);
  jb_fs_info_t info;
  hal_status_t status;

  cancel_state_t running = {0, 0, false};
  expected_state = &running;
  expect(install(b, p, base, canceled, &running, &status) == 0 &&
             status == HAL_OK,
         "install with a callback that does not cancel");
  expect(running.calls > 0, "the callback is polled while cmake runs");
  expect(running.foreign == 0, "the callback receives the caller's context");
  expect(jb_fs_status(jb_string_data(&archive), false, &info) == HAL_OK,
         "the completed install wrote the archive");

  expect(remove_tree(p), "remove the installed prefix");
  cancel_state_t stop = {0, 0, true};
  expected_state = &stop;
  expect(install(b, p, base, canceled, &stop, &status) == 130 &&
             status == HAL_ECANCELED,
         "a true callback cancels with exit code 130");
  expect(stop.calls > 0 && stop.foreign == 0,
         "the canceling callback receives the caller's context");

  expect(remove_tree(p), "remove the canceled prefix");
  expect(install(b, p, base, NULL, NULL, &status) == 0 && status == HAL_OK,
         "install without a callback");
done:
  expect(remove_tree(jb_string_data(&work)), "remove the fixtures");
  jb_string_destroy(&archive);
  jb_string_destroy(&prefix);
  jb_string_destroy(&rules);
  jb_string_destroy(&build);
  jb_string_destroy(&work);
  return failures == 0 ? 0 : 1;
}

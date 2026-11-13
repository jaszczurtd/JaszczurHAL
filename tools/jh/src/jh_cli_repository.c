#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "jh_cli_internal.h"
#include <jb/filesystem.h>
#include <jb/utf8.h>
#include <string.h>
#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <windows.h>
static volatile LONG interrupted = 0;
static BOOL WINAPI cancel_console(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
    return FALSE;
  InterlockedExchange(&interrupted, 1);
  return TRUE;
}
static bool canceled(void *user) {
  (void)user;
  return InterlockedCompareExchange(&interrupted, 0, 0) != 0;
}
#else
#include <errno.h>
#include <signal.h>
static volatile sig_atomic_t interrupted = 0;
static void cancel_signal(int signal_number) {
  (void)signal_number;
  interrupted = 1;
}
static bool canceled(void *user) {
  (void)user;
  return interrupted != 0;
}
#endif

static bool install_cancellation(jh_cli_t *ctx) {
#ifdef _WIN32
  return jh_cli_require(
      ctx,
      SetConsoleCtrlHandler(cancel_console, TRUE) != FALSE ? HAL_OK : HAL_EIO,
      3, "cannot install console cancellation");
#else
  struct sigaction action = {0};
  action.sa_handler = cancel_signal;
  (void)sigemptyset(&action.sa_mask);
  return jh_cli_require(
      ctx, sigaction(SIGINT, &action, NULL) == 0 ? HAL_OK : HAL_EIO, 3,
      "cannot install SIGINT cancellation");
#endif
}

static hal_status_t stream_bytes(FILE *stream, const char *data,
                                 size_t length) {
  while (length != 0) {
    if (canceled(NULL))
      return HAL_ECANCELED;
#ifdef _WIN32
    const HANDLE handle = (HANDLE)_get_osfhandle(_fileno(stream));
    const DWORD requested = (DWORD)(length > MAXDWORD ? MAXDWORD : length);
    DWORD written = 0;
    if (WriteFile(handle, data, requested, &written, NULL) == FALSE ||
        written == 0)
      return canceled(NULL) ? HAL_ECANCELED : HAL_EIO;
    const size_t count = written;
#else
    const size_t count = fwrite(data, 1, length, stream);
    if (count == 0) {
      if (ferror(stream) != 0 && errno == EINTR) {
        clearerr(stream);
        continue;
      }
      return HAL_EIO;
    }
#endif
    data += count;
    length -= count;
  }
  return HAL_OK;
}

typedef struct {
  bool capture;
  size_t limit;
  jb_string_t bytes;
} repository_output_t;

static hal_status_t repository_bytes(void *user, jb_process_stream_t stream,
                                     const unsigned char *data, size_t length) {
  repository_output_t *output = user;
  const char *bytes = (const char *)data;
  if (stream == JB_PROCESS_STDERR)
    return stream_bytes(stderr, bytes, length);
  if (!output->capture)
    return stream_bytes(stdout, bytes, length);
  if (length > output->limit - jb_string_size(&output->bytes))
    return HAL_EOVERFLOW;
  return jh_string_sink(&output->bytes, bytes, length);
}

static void result_paths(jh_cli_t *ctx, jb_json_span_t bytes) {
  jh_result_array(ctx, "paths");
  size_t begin = 0;
  while (begin < bytes.length && ctx->failure.status == HAL_OK) {
    const char *nul = memchr(bytes.data + begin, '\0', bytes.length - begin);
    const size_t end = nul == NULL ? bytes.length : (size_t)(nul - bytes.data);
    if (end != begin) {
      size_t bad = 0;
      const jb_json_span_t path = {bytes.data + begin, end - begin};
      if (!jh_cli_require(ctx, jb_utf8_validate(path.data, path.length, &bad),
                          2, "repository path is not UTF-8; use raw output"))
        break;
      if (ctx->failure.status == HAL_OK)
        (void)jh_cli_require(ctx, jb_json_writer_string(&ctx->writer, path), 4,
                             "cannot write path");
    }
    if (nul == NULL)
      break;
    begin = end + 1;
  }
  jh_result_array_end(ctx);
}

int jh_cli_repository(jh_cli_t *ctx) {
  const jh_arguments_t *args = &ctx->args;
  if (!jh_cli_value(ctx, opt_root, "repository files requires --root") ||
      !jh_cli_value(ctx, opt_git, "repository files requires --git"))
    return 0;
  bool absolute = false;
  if (!jh_cli_ok(ctx,
                 jb_fs_path_is_absolute(args->values[opt_git], &absolute)) ||
      !jh_cli_require(ctx, absolute ? HAL_OK : HAL_EINVAL, 2,
                      "--git requires an absolute executable path"))
    return 0;
  if (args->present[opt_max_bytes] && !ctx->json) {
    (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                         "repository --max-bytes requires --json");
    return 0;
  }
  const char *directory =
      jh_cli_path(ctx, args->values[opt_root], ctx->base, false);
  const char *executable =
      jh_cli_path(ctx, args->values[opt_git], ctx->base, false);
  const uint32_t timeout = (uint32_t)jh_cli_count(
      ctx, opt_timeout_ms, 0, UINT32_MAX,
      "--timeout-ms requires an integer between 0 and 4294967295");
  repository_output_t output = {
      ctx->json, ctx->json ? jh_cli_limit(ctx) : 0, {0}};
  (void)jh_cli_ok(ctx, jb_string_init(&output.bytes, jb_heap_allocator()));
  const char *child_args[] = {"-C",       directory,  "ls-files",
                              "--cached", "--others", "--exclude-standard",
                              "-z",       NULL};
  jb_process_options_t options = {0};
  options.executable = executable;
  options.arguments = child_args;
  options.working_directory = ctx->base;
  options.timeout_ms = timeout;
  options.output = repository_bytes;
  options.canceled = canceled;
  options.context = &output;
  if (!jh_cli_source(ctx, directory) || !install_cancellation(ctx))
    goto done;
  if (!jh_cli_require(
          ctx, fflush(stdout) == 0 && fflush(stderr) == 0 ? HAL_OK : HAL_EIO, 3,
          "cannot flush output before process launch"))
    goto done;
  ctx->process_attempted = true;
  const hal_status_t status = jb_process_run(&options, &ctx->process);
  if (!jh_cli_require(ctx, status,
                      status == HAL_ECANCELED                           ? 130
                      : status == HAL_ETIMEOUT                          ? 124
                      : status == HAL_ENOMEM || status == HAL_EOVERFLOW ? 4
                                                                        : 3,
                      "cannot enumerate repository files"))
    goto done;
  if (ctx->process.term_signal != 0) {
    (void)jh_cli_require(ctx, HAL_EIO, 128 + (int)ctx->process.term_signal,
                         "Git terminated by a signal");
    goto done;
  }
  if (ctx->process.exit_code != 0) {
    (void)jh_cli_error(
        ctx, HAL_EIO, (int)ctx->process.exit_code,
        JH_PARTS(jh_span("Git exited with code "),
                 jh_span(jh_cli_size(ctx, ctx->process.exit_code))));
    goto done;
  }
  if (ctx->json) {
    jh_result_begin(ctx, jb_string_size(&output.bytes) + strlen(directory), 32);
    jh_result_text(ctx, "command", "repository.files");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "root", directory);
    result_paths(ctx, jh_string_span(&output.bytes));
    jh_result_process(ctx);
    (void)jh_result_finish(ctx);
  } else
    (void)jh_cli_require(
        ctx, fflush(stdout) == 0 && fflush(stderr) == 0 ? HAL_OK : HAL_EIO, 3,
        "cannot flush repository output");
done:
  jb_string_destroy(&output.bytes);
  return 0;
}

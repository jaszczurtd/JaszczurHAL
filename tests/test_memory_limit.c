/* The memory ceiling of the native test programs
 * (tests/support/jh_test_memory_limit.c): a copy of this program that
 * allocates without an end is stopped before it has used the ceiling. */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SANITIZER_STOPS_THE_PROCESS 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SANITIZER_STOPS_THE_PROCESS 1
#endif
#endif

#define BLOCK_BYTES ((size_t)16 << 20)
#define EXIT_NOT_LIMITED 42
#define RUNAWAY_ARGUMENT "allocate-without-end"

/* Allocates and writes blocks until allocation fails. Reaching the ceiling in
 * blocks of its own means that nothing limited the process. */
static int allocate_without_end(void) {
  const size_t ceiling = (size_t)JH_TEST_MEMORY_LIMIT_MB << 20;
  for (size_t total = 0; total < ceiling; total += BLOCK_BYTES) {
    char *volatile block = malloc(BLOCK_BYTES);
    if (block == NULL)
      return EXIT_SUCCESS;
    memset(block, 1, BLOCK_BYTES);
  }
  return EXIT_NOT_LIMITED;
}

/* Reads the diagnostics of the child to their end and reports whether their
 * first part names the resident size limit of the sanitizer runtime. */
static bool sanitizer_limit_reported(int descriptor) {
  char text[4096];
  size_t length = 0;
  for (;;) {
    char chunk[4096];
    const ssize_t count = read(descriptor, chunk, sizeof(chunk));
    if (count <= 0)
      break;
    const size_t room = sizeof(text) - 1 - length;
    const size_t kept = (size_t)count < room ? (size_t)count : room;
    memcpy(text + length, chunk, kept);
    length += kept;
  }
  text[length] = '\0';
  return strstr(text, "hard rss limit exhausted") != NULL;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], RUNAWAY_ARGUMENT) == 0)
    return allocate_without_end();

  int diagnostics[2];
  if (pipe(diagnostics) != 0) {
    perror("memory limit: pipe");
    return EXIT_FAILURE;
  }
  posix_spawn_file_actions_t actions;
  char runaway[] = RUNAWAY_ARGUMENT;
  char *const arguments[] = {argv[0], runaway, NULL};
  pid_t child = 0;
  int error = posix_spawn_file_actions_init(&actions);
  if (error == 0) {
    error = posix_spawn_file_actions_adddup2(&actions, diagnostics[1],
                                             STDERR_FILENO);
    if (error == 0)
      error = posix_spawn_file_actions_addclose(&actions, diagnostics[0]);
    if (error == 0)
      error = posix_spawn_file_actions_addclose(&actions, diagnostics[1]);
    /* The path of this program as it was started: under Valgrind
     * /proc/self/exe names the Valgrind tool. */
    if (error == 0)
      error = posix_spawnp(&child, argv[0], &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
  }
  if (error != 0) {
    fprintf(stderr, "memory limit: cannot start the child: %s\n",
            strerror(error));
    return EXIT_FAILURE;
  }
  close(diagnostics[1]);
  const bool reported = sanitizer_limit_reported(diagnostics[0]);
  close(diagnostics[0]);

  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    perror("memory limit: waitpid");
    return EXIT_FAILURE;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_NOT_LIMITED) {
    fprintf(stderr,
            "memory limit: a test process allocated %d MiB without a limit\n",
            JH_TEST_MEMORY_LIMIT_MB);
    return EXIT_FAILURE;
  }
#if defined(SANITIZER_STOPS_THE_PROCESS)
  if (!reported || (WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
    fprintf(stderr,
            "memory limit: the sanitizer runtime did not stop the child "
            "(status 0x%x)\n",
            (unsigned)status);
    return EXIT_FAILURE;
  }
#else
  (void)reported;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
    fprintf(stderr,
            "memory limit: allocation in the child did not fail cleanly "
            "(status 0x%x)\n",
            (unsigned)status);
    return EXIT_FAILURE;
  }
#endif
  puts("memory limit test passed");
  return EXIT_SUCCESS;
}

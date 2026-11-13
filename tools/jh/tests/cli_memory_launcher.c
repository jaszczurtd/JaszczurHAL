/* Execute the actual CLI under the memory checker, keeping its PID for the
 * SIGINT regression. The CMake runner checks reports independently of exits
 * expected by individual negative tests. Only the CLI runs under Valgrind. */
#define _POSIX_C_SOURCE 200809L
#include <jb/array.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *program = getenv("JH_TEST_PROGRAM");
  const char *reports = getenv("JH_TEST_REPORTS");
  const char *valgrind = getenv("JH_TEST_VALGRIND");
  if (!program || !*program) {
    fputs("JH_TEST_PROGRAM must name the tested executable\n", stderr);
    return 125;
  }
  argv[0] = (char *)program;
  if (!reports || !*reports) {
    execv(program, argv);
    perror("execv CLI");
    return 125;
  }
  const size_t length = strlen(reports);
  if (length > SIZE_MAX - 80u)
    return 125;
  char *path = malloc(length + 80u);
  if (!path)
    return 125;
  (void)snprintf(path, length + 80u, "%s/run.%ld", reports, (long)getpid());
  FILE *marker = fopen(path, "wx");
  if (!marker) {
    perror("create CLI memory marker");
    free(path);
    return 125;
  }
  const int written = fprintf(marker, "%s\n", program);
  const int closed = fclose(marker);
  if (written < 0 || closed != 0) {
    free(path);
    return 125;
  }
  if (!valgrind || !*valgrind) {
    free(path);
    execv(program, argv);
    perror("execv instrumented CLI");
    return 125;
  }
  (void)snprintf(path, length + 80u, "--xml-file=%s/valgrind.%%p.xml", reports);
  char *prefix[] = {(char *)valgrind,
                    "--tool=memcheck",
                    "--xml=yes",
                    path,
                    "--leak-check=full",
                    "--show-leak-kinds=definite,indirect,possible",
                    "--errors-for-leak-kinds=definite,indirect,possible",
                    "--track-origins=yes",
                    "--error-exitcode=97"};
  const size_t count = (size_t)argc + COUNTOF(prefix) + 1u;
  if (count > SIZE_MAX / sizeof(char *)) {
    free(path);
    return 125;
  }
  char **command = calloc(count, sizeof(*command));
  if (!command) {
    free(path);
    return 125;
  }
  memcpy(command, prefix, sizeof(prefix));
  memcpy(command + COUNTOF(prefix), argv, (size_t)argc * sizeof(*argv));
  execv(valgrind, command);
  perror("execv Valgrind");
  free(command);
  free(path);
  return 125;
}

/* Memory ceiling of one native test process on Linux.
 *
 * A defect in the code under test can turn a test input into allocation that
 * never ends. Linux then lets the process grow until the kernel kills
 * processes of its own choice, and a desktop session may be stopped with them.
 * Linked into a test executable, this file makes such a run end inside the
 * test: allocation fails, or the sanitizer runtime stops the process.
 *
 * cmake/jh_test_memory_limit.cmake adds this file to the test executables and
 * passes JH_TEST_MEMORY_LIMIT_MB. It is not part of the HAL library or of any
 * firmware.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

#if !defined(__linux__)
#error "jh_test_memory_limit.c supports Linux only"
#endif
#if !defined(JH_TEST_MEMORY_LIMIT_MB)
#error "JH_TEST_MEMORY_LIMIT_MB is not defined"
#endif

/* 1 MiB to 1 TiB: the shift below stays inside rlim_t. */
_Static_assert(JH_TEST_MEMORY_LIMIT_MB >= 1 &&
                   JH_TEST_MEMORY_LIMIT_MB <= 1048576,
               "JH_TEST_MEMORY_LIMIT_MB is out of range");

#define JH_TEST_MEMORY_TEXT_(value) #value
#define JH_TEST_MEMORY_TEXT(value) JH_TEST_MEMORY_TEXT_(value)

#if defined(__SANITIZE_ADDRESS__)
#define JH_TEST_MEMORY_ASAN 1
#elif defined(__SANITIZE_THREAD__)
#define JH_TEST_MEMORY_TSAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define JH_TEST_MEMORY_ASAN 1
#elif __has_feature(thread_sanitizer)
#define JH_TEST_MEMORY_TSAN 1
#endif
#endif

/* The shadow memory of ASan and TSan reserves terabytes of address space, so
 * an address space limit cannot be used there. Their runtimes read these
 * default options and stop the process above the given resident size;
 * ASAN_OPTIONS and TSAN_OPTIONS of the environment add to them. */
#if defined(JH_TEST_MEMORY_ASAN)
const char *__asan_default_options(void);
const char *__asan_default_options(void) {
  return "hard_rss_limit_mb=" JH_TEST_MEMORY_TEXT(JH_TEST_MEMORY_LIMIT_MB);
}
#elif defined(JH_TEST_MEMORY_TSAN)
const char *__tsan_default_options(void);
const char *__tsan_default_options(void) {
  return "hard_rss_limit_mb=" JH_TEST_MEMORY_TEXT(JH_TEST_MEMORY_LIMIT_MB);
}
#else
/* Lowers the soft address space limit before main(); a lower limit of the
 * caller stays. Under Valgrind the limit covers Valgrind itself as well. A
 * test that cannot be limited does not run. */
__attribute__((constructor)) static void jh_test_memory_limit(void) {
  struct rlimit limit;
  if (getrlimit(RLIMIT_AS, &limit) != 0) {
    perror("test memory limit: getrlimit(RLIMIT_AS)");
    _exit(125);
  }
  const rlim_t wanted = (rlim_t)JH_TEST_MEMORY_LIMIT_MB << 20;
  if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur <= wanted)
    return;
  limit.rlim_cur = wanted;
  if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < wanted)
    limit.rlim_cur = limit.rlim_max;
  if (setrlimit(RLIMIT_AS, &limit) != 0) {
    perror("test memory limit: setrlimit(RLIMIT_AS)");
    _exit(125);
  }
}
#endif

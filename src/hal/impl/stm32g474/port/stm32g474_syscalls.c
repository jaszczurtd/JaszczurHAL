/**
 * @file stm32g474_syscalls.c
 * @brief Minimal newlib syscall stubs for the bare-metal STM32G474 firmware.
 *
 * newlib-nano (selected via --specs=nano.specs) references the low-level
 * syscalls _read/_write/_close/_lseek/_isatty/_fstat/_getpid/_kill from its
 * reentrant wrappers. When these are not provided by the project, the linker
 * pulls the libnosys (--specs=nosys.specs) fallback stubs, which carry
 * `.gnu.warning.<symbol>` sections and emit
 * "warning: <syscall> is not implemented and will always fail" at link time.
 *
 * Providing our own strong definitions here resolves those symbols from this
 * translation unit, so the warning-carrying libnosys members are never pulled
 * and the link is warning-clean. The stubs deliberately keep the historical
 * "no host I/O" behaviour: the firmware uses its dedicated debug UART
 * (g474_debug_uart.c), not newlib stdio, so stdout/stderr writes are discarded.
 * _sbrk and _exit are intentionally left to libnosys (they do not warn and the
 * heap layout is owned by the linker script).
 */
#include "../../../hal_target.h"

/* Bare-metal runtime/retarget tier: defined only on the real ARM target
 * (JH_STM32G474_HW), never on the host sanity build where glibc owns these
 * syscalls. See doc/future_ideas.md section 6. */
#ifdef JH_STM32G474_HW

#include <errno.h>
#include <sys/stat.h>

int _close(int fd) {
  (void)fd;
  errno = ENOSYS;
  return -1;
}

int _fstat(int fd, struct stat *st) {
  (void)fd;
  st->st_mode = S_IFCHR; /* character device, like a terminal */
  return 0;
}

int _isatty(int fd) {
  (void)fd;
  return 1;
}

int _lseek(int fd, int ptr, int dir) {
  (void)fd;
  (void)ptr;
  (void)dir;
  return 0;
}

int _read(int fd, char *ptr, int len) {
  (void)fd;
  (void)ptr;
  (void)len;
  return 0; /* EOF: no host input */
}

int _write(int fd, char *ptr, int len) {
  (void)fd;
  (void)ptr;
  return len; /* discard: debug output goes through g474_debug_uart.c */
}

int _getpid(void) { return 1; }

int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  errno = ENOSYS;
  return -1;
}

#endif /* JH_STM32G474_HW */

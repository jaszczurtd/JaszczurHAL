/**
 * @file stm32g474_syscalls.c
 * @brief newlib retarget layer for bare-metal STM32G474 firmware.
 *
 * This runtime/BSP tier sits below libc and the public HAL. It may depend on
 * bare-metal port primitives such as g474_debug_uart and linker symbols, but it
 * must not call app-facing HAL modules such as hal_serial.
 */
#include "hal/hal_target.h"

#ifdef JH_STM32G474_HW

#include "../g474_debug_uart.h"
#include "../stm32g474_regs.h"

#if defined(HAL_ENABLE_FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

#include <errno.h>
#include <limits.h>
#include <reent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define JH_STDIN_FD 0
#define JH_STDOUT_FD 1
#define JH_STDERR_FD 2

extern char end;
extern char _estack;
extern char _Min_Stack_Size;

static uintptr_t s_heap_current = 0u;

__attribute__((weak)) int
jh_stm32g474_runtime_gettimeofday(struct timeval *time_value) {
  (void)time_value;
  errno = ENOSYS;
  return -1;
}

#if defined(HAL_ENABLE_FREERTOS)
static int stm32g474_runtime_in_isr(void) {
  uint32_t ipsr;
  __asm volatile("MRS %0, ipsr" : "=r"(ipsr));
  return (ipsr & 0x1FFu) != 0u;
}
#endif

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t alignment) {
  return (value + (alignment - 1u)) & ~(alignment - 1u);
}

static uintptr_t heap_base(void) {
  return align_up_uintptr((uintptr_t)&end, 8u);
}

static uintptr_t heap_limit(void) {
  const uintptr_t stack_top = (uintptr_t)&_estack;
  const uintptr_t min_stack = (uintptr_t)&_Min_Stack_Size;
  return (stack_top - min_stack) & ~(uintptr_t)7u;
}

uint32_t stm32g474_runtime_heap_total_bytes(void) {
  return (uint32_t)(heap_limit() - heap_base());
}

uint32_t stm32g474_runtime_heap_free_bytes(void) {
  const uintptr_t current = s_heap_current == 0u ? heap_base() : s_heap_current;
  const uintptr_t limit = heap_limit();
  return current < limit ? (uint32_t)(limit - current) : 0u;
}

static void stm32g474_runtime_reset(void) __attribute__((noreturn));
static void stm32g474_runtime_reset(void) {
  SCB_AIRCR =
      (SCB_AIRCR & 0x00000700u) | SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb" ::: "memory");
  for (;;) {
  }
}

int _close(int fd) {
  if ((fd >= JH_STDIN_FD) && (fd <= JH_STDERR_FD)) {
    return 0;
  }
  errno = EBADF;
  return -1;
}

int _fstat(int fd, struct stat *st) {
  if ((fd < JH_STDIN_FD) || (fd > JH_STDERR_FD)) {
    errno = EBADF;
    return -1;
  }
  if (st == NULL) {
    errno = EINVAL;
    return -1;
  }

  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int fd) {
  if ((fd >= JH_STDIN_FD) && (fd <= JH_STDERR_FD)) {
    return 1;
  }
  errno = EBADF;
  return 0;
}

int _gettimeofday(struct timeval *time_value, void *timezone) {
  (void)timezone;
  if (time_value == NULL) {
    errno = EINVAL;
    return -1;
  }
  return jh_stm32g474_runtime_gettimeofday(time_value);
}

_off_t _lseek(int fd, _off_t offset, int whence) {
  (void)offset;
  (void)whence;

  if ((fd < JH_STDIN_FD) || (fd > JH_STDERR_FD)) {
    errno = EBADF;
    return (_off_t)-1;
  }

  errno = ESPIPE;
  return (_off_t)-1;
}

int _read(int fd, char *ptr, int len) {
  if (fd != JH_STDIN_FD) {
    errno = EBADF;
    return -1;
  }
  if (ptr == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (len <= 0) {
    return 0;
  }

  g474_debug_uart_init();

  int count = 0;
  while (count < len) {
    const int ch = g474_debug_uart_getc_nonblock();
    if (ch < 0) {
      break;
    }
    ptr[count++] = (char)ch;
  }

  if (count == 0) {
    errno = EAGAIN;
    return -1;
  }
  return count;
}

int _write(int fd, char *ptr, int len) {
  if ((fd != JH_STDOUT_FD) && (fd != JH_STDERR_FD)) {
    errno = EBADF;
    return -1;
  }
  if (ptr == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (len <= 0) {
    return 0;
  }

  g474_debug_uart_init();

  for (int i = 0; i < len; ++i) {
    const char ch = ptr[i];
    if (ch == '\n') {
      g474_debug_uart_putc('\r');
    }
    g474_debug_uart_putc(ch);
  }

  return len;
}

void *_sbrk(ptrdiff_t incr) {
  const uintptr_t base = heap_base();
  const uintptr_t limit = heap_limit();

  if (s_heap_current == 0u) {
    s_heap_current = base;
  }

  const uintptr_t current = s_heap_current;
  uintptr_t next;

  if (incr >= 0) {
    const uintptr_t add = (uintptr_t)incr;
    if (add > (UINTPTR_MAX - current)) {
      errno = ENOMEM;
      return (void *)-1; // NOLINT(performance-no-int-to-ptr): newlib ABI
    }
    next = align_up_uintptr(current + add, 8u);
    if ((next < current) || (next > limit)) {
      errno = ENOMEM;
      return (void *)-1; // NOLINT(performance-no-int-to-ptr): newlib ABI
    }
  } else {
    const uintptr_t sub = (uintptr_t)(-(incr + 1)) + 1u;
    if (sub > (current - base)) {
      errno = EINVAL;
      return (void *)-1; // NOLINT(performance-no-int-to-ptr): newlib ABI
    }
    next = current - sub;
  }

  s_heap_current = next;
  return (void *)current; // NOLINT(performance-no-int-to-ptr): newlib ABI
}

void __malloc_lock(struct _reent *reent) {
  (void)reent;
#if defined(HAL_ENABLE_FREERTOS)
  if (!stm32g474_runtime_in_isr()) {
    vTaskSuspendAll();
  }
#endif
}

void __malloc_unlock(struct _reent *reent) {
  (void)reent;
#if defined(HAL_ENABLE_FREERTOS)
  if (!stm32g474_runtime_in_isr()) {
    (void)xTaskResumeAll();
  }
#endif
}

int _getpid(void) { return 1; }

int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  stm32g474_runtime_reset();
}

void _exit(int status) {
  (void)status;
  stm32g474_runtime_reset();
}

#endif /* JH_STM32G474_HW */

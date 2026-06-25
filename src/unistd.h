#pragma once

/**
 * @file unistd.h
 * @brief Minimal POSIX I/O declarations used by the BSD socket adapter.
 */

#if defined(__has_include_next)
#if __has_include_next(<unistd.h>)
#include_next <unistd.h>
#define HAL_BSD_SOCKET_INCLUDED_SYSTEM_UNISTD 1
#endif
#endif

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

#ifdef __cplusplus
}
#endif

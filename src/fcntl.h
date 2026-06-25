#pragma once

/**
 * @file fcntl.h
 * @brief Minimal POSIX fcntl declarations used by the BSD socket adapter.
 */

#if defined(__has_include_next)
#if __has_include_next(<fcntl.h>)
#include_next <fcntl.h>
#define HAL_BSD_SOCKET_INCLUDED_SYSTEM_FCNTL 1
#endif
#endif

#ifndef F_GETFL
#define F_GETFL 3
#endif

#ifndef F_SETFL
#define F_SETFL 4
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 04000
#endif

#ifdef __cplusplus
extern "C" {
#endif

int fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

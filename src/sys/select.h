#pragma once

/**
 * @file sys/select.h
 * @brief Minimal POSIX select declarations used by the BSD socket adapter.
 */

#include <stdint.h>

#if defined(__has_include_next)
#if __has_include_next(<sys/select.h>)
#include_next <sys/select.h>
#define HAL_BSD_SOCKET_INCLUDED_SYSTEM_SELECT 1
#endif
#endif

#ifndef HAL_BSD_SOCKET_INCLUDED_SYSTEM_SELECT

#ifndef FD_SETSIZE
#define FD_SETSIZE 128
#endif

typedef struct {
  uint32_t bits[(FD_SETSIZE + 31) / 32];
} fd_set;

struct timeval {
  long tv_sec;
  long tv_usec;
};

#define FD_ZERO(set_ptr)                                                       \
  do {                                                                         \
    fd_set *hal_bsd_fd_zero_set = (set_ptr);                                   \
    for (int hal_bsd_fd_zero_i = 0;                                            \
         hal_bsd_fd_zero_i < (int)(sizeof(hal_bsd_fd_zero_set->bits) /         \
                                   sizeof(hal_bsd_fd_zero_set->bits[0]));      \
         ++hal_bsd_fd_zero_i) {                                                \
      hal_bsd_fd_zero_set->bits[hal_bsd_fd_zero_i] = 0u;                       \
    }                                                                          \
  } while (0)

#define FD_SET(fd, set_ptr)                                                    \
  do {                                                                         \
    const int hal_bsd_fd_set_fd = (fd);                                        \
    if (hal_bsd_fd_set_fd >= 0 && hal_bsd_fd_set_fd < FD_SETSIZE) {            \
      (set_ptr)->bits[hal_bsd_fd_set_fd / 32] |=                               \
          (uint32_t)(1u << (hal_bsd_fd_set_fd % 32));                          \
    }                                                                          \
  } while (0)

#define FD_CLR(fd, set_ptr)                                                    \
  do {                                                                         \
    const int hal_bsd_fd_clr_fd = (fd);                                        \
    if (hal_bsd_fd_clr_fd >= 0 && hal_bsd_fd_clr_fd < FD_SETSIZE) {            \
      (set_ptr)->bits[hal_bsd_fd_clr_fd / 32] &=                               \
          (uint32_t) ~(1u << (hal_bsd_fd_clr_fd % 32));                        \
    }                                                                          \
  } while (0)

#define FD_ISSET(fd, set_ptr)                                                  \
  (((fd) >= 0 && (fd) < FD_SETSIZE)                                            \
       ? (((set_ptr)->bits[(fd) / 32] & (uint32_t)(1u << ((fd) % 32))) != 0u)  \
       : 0)

#endif /* HAL_BSD_SOCKET_INCLUDED_SYSTEM_SELECT */

#ifdef __cplusplus
extern "C" {
#endif

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);

#ifdef __cplusplus
}
#endif

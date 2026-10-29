#ifndef JH_TESTS_LWIP_HOST_ARCH_CC_H
#define JH_TESTS_LWIP_HOST_ARCH_CC_H

/* Host port of the real lwIP core used by tests/lwip_host. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#define LWIP_PLATFORM_DIAG(message)                                            \
  do {                                                                         \
    printf message;                                                            \
  } while (0)

/* An lwIP assertion is a test failure: abort so CTest and Valgrind see it. */
#define LWIP_PLATFORM_ASSERT(message)                                          \
  do {                                                                         \
    fprintf(stderr, "lwIP assertion: %s (%s:%d)\n", message, __FILE__,         \
            __LINE__);                                                         \
    abort();                                                                   \
  } while (0)

#define LWIP_RAND() ((u32_t)rand())

typedef unsigned int sys_prot_t;

#endif

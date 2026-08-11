#ifndef JASZCZURHAL_LWIP_ARCH_CC_H
#define JASZCZURHAL_LWIP_ARCH_CC_H

#include "hal/core/hal_compiler.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t jh_lwip_port_rand(void);
HAL_NORETURN void jh_lwip_port_assert(const char *message, const char *file,
                                      int line);

#ifdef __cplusplus
}
#endif

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#ifndef LWIP_RAND
#define LWIP_RAND() jh_lwip_port_rand()
#endif
#ifndef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG(message)                                            \
  do {                                                                         \
    (void)0;                                                                   \
  } while (0)
#endif
#ifndef LWIP_PLATFORM_ASSERT
#define LWIP_PLATFORM_ASSERT(message)                                          \
  jh_lwip_port_assert((message), __FILE__, __LINE__)
#endif

typedef unsigned int sys_prot_t;

#endif

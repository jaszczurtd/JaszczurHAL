#ifndef JASZCZURHAL_LWIP_ARCH_CC_H
#define JASZCZURHAL_LWIP_ARCH_CC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t jh_lwip_port_rand(void);
void jh_lwip_port_assert(const char *message, const char *file, int line);

#ifdef __cplusplus
}
#endif

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_RAND() jh_lwip_port_rand()
#define LWIP_PLATFORM_DIAG(message)                                            \
  do {                                                                         \
    (void)0;                                                                   \
  } while (0)
#define LWIP_PLATFORM_ASSERT(message)                                          \
  jh_lwip_port_assert((message), __FILE__, __LINE__)

typedef unsigned int sys_prot_t;

#endif

#include "hal/core/hal_array.h"
#include "hal/core/hal_memory.h"
#include <stdint.h>

static uint64_t HAL_CPU_ONLY_BSS(c_buffer)[4];

int cpu_only_bss_c_probe(void);
int cpu_only_bss_c_probe(void) {
  for (unsigned i = 0; i < COUNTOF(c_buffer); ++i) {
    if (c_buffer[i] != 0u) {
      return 1;
    }
  }
  c_buffer[3] = UINT64_C(0x123456789abcdef0);
  return c_buffer[3] != UINT64_C(0x123456789abcdef0);
}

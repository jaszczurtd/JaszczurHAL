#include "hal/core/hal_array.h"
#include "hal/core/hal_memory.h"
#include <cstdint>

extern "C" int cpu_only_bss_c_probe(void);

alignas(32) static uint64_t HAL_CPU_ONLY_BSS(cpp_buffer)[4] = {};
static bool constructor_saw_zero;

struct zero_probe {
  zero_probe() {
    constructor_saw_zero = true;
    for (unsigned i = 0; i < COUNTOF(cpp_buffer); ++i) {
      constructor_saw_zero = constructor_saw_zero && cpp_buffer[i] == 0u;
    }
  }
};
static zero_probe probe;

int main() {
  if (!constructor_saw_zero ||
      reinterpret_cast<uintptr_t>(cpp_buffer) % 32u != 0u ||
      cpu_only_bss_c_probe() != 0) {
    return 1;
  }
  cpp_buffer[3] = UINT64_C(0x123456789abcdef0);
  return cpp_buffer[3] != UINT64_C(0x123456789abcdef0);
}

/* Built with both compiler identities forced to zero, so the portable
 * fallback that no real compiler selects still gets compiled and can be
 * compared against the builtin path in the same test run. */
#include "hal/hal_compiler.h"

#include <cstdint>

#if HAL_COMPILER_IS_GNU_LIKE || HAL_COMPILER_IS_MSVC
#error "this translation unit must build the portable fallback"
#endif

static HAL_NORETURN void portable_abort(void) { HAL_TRAP(); }

uint32_t portable_clz32_probe(uint32_t value) { return hal_clz32(value); }

void portable_abort_probe(int guard) {
  if (guard > 100000) {
    portable_abort();
  }
}

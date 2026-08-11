#include "hal/core/hal_compiler.h"
#include "hal/security/hal_crc.h"

#include <cstdint>
#include <cstdlib>

HAL_PACKED_BEGIN
struct packed_probe {
  uint8_t tag;
  uint32_t value;
} HAL_PACKED;
HAL_PACKED_END

static_assert(sizeof(packed_probe) == 5u,
              "HAL_PACKED must remove structure padding");

/* Defined by test_host_compiler_portable.cpp with the fallback forced on. */
extern uint32_t portable_clz32_probe(uint32_t value);
extern void portable_abort_probe(int guard);

static HAL_FORCE_INLINE uint32_t leading_zero_span(uint32_t value) {
  return hal_clz32(value);
}

static HAL_NORETURN void fail(int code) { exit(code); }

/* Compiles the abort paths without executing them: the guard below depends on
 * a runtime value, so no compiler can prune this call as dead code. */
static HAL_NORETURN void never_taken(void) {
  HAL_TRAP();
  HAL_UNREACHABLE();
}

int main(int argc, char **) {
  static constexpr uint8_t check[] = {'1', '2', '3', '4', '5',
                                      '6', '7', '8', '9'};

  if (hal_crc8_maxim(check, sizeof(check)) != 0xA1u) {
    fail(1);
  }
  if (hal_crc16_ccitt(check, sizeof(check), HAL_CRC16_CCITT_INIT) != 0x29B1u) {
    fail(2);
  }
  if (hal_crc32(check, sizeof(check)) != 0xCBF43926u) {
    fail(3);
  }

  if (leading_zero_span(1u) != 31u || leading_zero_span(0x80000000u) != 0u ||
      leading_zero_span(0xFFFFu) != 16u) {
    fail(4);
  }

  const packed_probe probe = {0x5Au, 0x11223344u};
  if (probe.tag != 0x5Au || probe.value != 0x11223344u) {
    fail(5);
  }

  /* The portable fallback must agree with this compiler's builtin for every
   * value the contract defines, which is everything except zero. */
  static constexpr uint32_t spans[] = {1u,      2u,          0x7Fu,
                                       0xFFFFu, 0x00010000u, 0x80000000u};
  for (uint32_t value : spans) {
    if (portable_clz32_probe(value) != hal_clz32(value)) {
      fail(6);
    }
  }

  if (argc > 100000) {
    portable_abort_probe(argc);
    never_taken();
  }
  return 0;
}

#pragma once

/**
 * @file hal_compiler.h
 * @brief One source for the compiler extensions JaszczurHAL relies on.
 *
 * Firmware always builds with GNU toolchains, while host targets also build
 * with Clang and MSVC. Every compiler-specific attribute or builtin used by
 * JaszczurHAL code belongs here, so a new host compiler is a change in one
 * file instead of a sweep through the tree.
 *
 * Scope rules:
 * - JaszczurHAL code uses these macros, including the port headers it owns;
 * - vendored third-party sources keep their upstream form, because matching
 *   upstream simplifies updates and those sources never see a second compiler;
 * - linker-level attributes (@c section, @c naked, @c constructor) and inline
 *   assembly stay explicit at their target-specific call sites, where a wrong
 *   mapping would silently corrupt the memory map.
 *
 * The header is standalone: it pulls in nothing from the rest of the HAL, so
 * runtime, port and test translation units can include it directly.
 */

#include <stdint.h>

/* Both identities may be pre-defined to 0 to force the portable fallback.
 * Tests use that to build a branch no real compiler selects; an exotic port
 * can use it before its own mapping exists. */
#ifndef HAL_COMPILER_IS_MSVC
#if defined(_MSC_VER) && !defined(__clang__)
#define HAL_COMPILER_IS_MSVC 1
#else
#define HAL_COMPILER_IS_MSVC 0
#endif
#endif

#ifndef HAL_COMPILER_IS_GNU_LIKE
#if defined(__GNUC__) || defined(__clang__)
#define HAL_COMPILER_IS_GNU_LIKE 1
#else
#define HAL_COMPILER_IS_GNU_LIKE 0
#endif
#endif

#if HAL_COMPILER_IS_MSVC || !HAL_COMPILER_IS_GNU_LIKE
#include <stdlib.h>
#endif
#if HAL_COMPILER_IS_MSVC
#include <intrin.h>
#endif

/** @def HAL_NORETURN
 *  @brief Declares a function that never returns.
 *  @note Place it after the storage class and before the return type
 *  (@c static @c HAL_NORETURN @c void @c f(void)); MSVC rejects some other
 *  positions for @c __declspec.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_NORETURN __attribute__((noreturn))
#elif HAL_COMPILER_IS_MSVC
#define HAL_NORETURN __declspec(noreturn)
#else
#define HAL_NORETURN
#endif

/** @def HAL_NO_STACK_PROTECTOR
 *  @brief Excludes a function from compiler stack-canary instrumentation.
 *
 *  Use only in the stack-protector runtime and its terminal fault/reset path.
 *  Those functions must remain callable after the compiler has detected a
 *  damaged stack frame and must not recursively depend on the same canary.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_NO_STACK_PROTECTOR __attribute__((no_stack_protector))
#else
#define HAL_NO_STACK_PROTECTOR
#endif

/** @def HAL_FORCE_INLINE
 *  @brief Requests inlining regardless of the optimizer's own decision.
 *
 *  The macro carries the inline specifier itself. Writing @c inline next to it
 *  produces a duplicate specifier on GNU and warning C4141 on MSVC, so use it
 *  alone: @c static @c HAL_FORCE_INLINE @c uint32_t @c f(void).
 *  @note Only for timing-critical target code that documents why it needs it.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_FORCE_INLINE inline __attribute__((always_inline))
#elif HAL_COMPILER_IS_MSVC
#define HAL_FORCE_INLINE __forceinline
#else
#define HAL_FORCE_INLINE inline
#endif

/** @def HAL_TRAP
 *  @brief Stops execution immediately at an unrecoverable point.
 *
 *  GNU builds emit a trapping instruction. MSVC breaks into the debugger and
 *  then aborts, because @c __debugbreak() alone returns to the caller.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_TRAP() __builtin_trap()
#elif HAL_COMPILER_IS_MSVC
#define HAL_TRAP()                                                             \
  do {                                                                         \
    __debugbreak();                                                            \
    abort();                                                                   \
  } while (0)
#else
#define HAL_TRAP() abort()
#endif

/** @def HAL_UNREACHABLE
 *  @brief Marks a path the program must never take.
 *  @note Reaching it is undefined behaviour on GNU and MSVC; use @c HAL_TRAP()
 *  when the condition can occur at runtime and must be diagnosable.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_UNREACHABLE() __builtin_unreachable()
#elif HAL_COMPILER_IS_MSVC
#define HAL_UNREACHABLE() __assume(0)
#else
#define HAL_UNREACHABLE() HAL_TRAP()
#endif

/** @def HAL_PACKED
 *  @brief Removes padding from a structure definition.
 *
 *  MSVC has no packing attribute, so the pragma pair is part of the contract.
 *  Wrap every packed definition:
 *  @code
 *  HAL_PACKED_BEGIN
 *  struct wire_header {
 *    uint8_t kind;
 *    uint32_t length;
 *  } HAL_PACKED;
 *  HAL_PACKED_END
 *  @endcode
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_PACKED __attribute__((__packed__))
#define HAL_PACKED_BEGIN
#define HAL_PACKED_END
#elif HAL_COMPILER_IS_MSVC
#define HAL_PACKED
#define HAL_PACKED_BEGIN __pragma(pack(push, 1))
#define HAL_PACKED_END __pragma(pack(pop))
#else
#define HAL_PACKED
#define HAL_PACKED_BEGIN
#define HAL_PACKED_END
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Count leading zero bits of a 32-bit value.
 * @param value Non-zero input; zero is undefined for the GNU builtin and is
 *              reported as 32 here only on compilers without one.
 * @return Number of leading zero bits, 0..31 for a non-zero @p value.
 */
static inline uint32_t hal_clz32(uint32_t value) {
#if HAL_COMPILER_IS_GNU_LIKE
  return (uint32_t)__builtin_clz(value);
#elif HAL_COMPILER_IS_MSVC
  unsigned long index = 0ul;
  return _BitScanReverse(&index, (unsigned long)value) ? (31u - (uint32_t)index)
                                                       : 32u;
#else
  uint32_t count = 0u;
  uint32_t probe = 0x80000000u;
  while (probe != 0u && (value & probe) == 0u) {
    ++count;
    probe >>= 1;
  }
  return count;
#endif
}

#ifdef __cplusplus
}
#endif

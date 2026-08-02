#pragma once

/**
 * @file hal_assert.h
 * @brief Portable HAL assertion API.
 */

#include "hal_compiler.h"

/**
 * @def HAL_ASSERT(cond, msg)
 * Lightweight assert for HAL resource exhaustion.
 *
 * When the condition is false the macro calls @c hal_assert_fail(), whose
 * implementation is selected by the exact HAL target. Hardware builds print
 * @p msg through the target debug channel and enter an infinite loop so the
 * watchdog can reset the system; mock/test builds call @c abort().
 *
 * Define @c HAL_DISABLE_ASSERTS before including this header (or via a
 * compiler flag) to compile all HAL_ASSERTs to no-ops, removing both the text
 * overhead and the branch from release builds.
 *
 * @code
 *   HAL_ASSERT(ptr != NULL, "hal_can: pool exhausted");
 * @endcode
 */
#ifdef HAL_DISABLE_ASSERTS

#define HAL_ASSERT(cond, msg) ((void)0)

#else /* asserts enabled (default) */

#ifdef __cplusplus
extern "C" {
#endif

HAL_NORETURN void hal_assert_fail(const char *msg);

#ifdef __cplusplus
}
#endif

#define HAL_ASSERT(cond, msg)                                                  \
  do {                                                                         \
    if (!(cond)) {                                                             \
      hal_assert_fail((msg));                                                  \
    }                                                                          \
  } while (0)

#endif /* HAL_DISABLE_ASSERTS */

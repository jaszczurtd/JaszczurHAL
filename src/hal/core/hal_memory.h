#pragma once

/** @file hal_memory.h
 * @brief Placement of time-sensitive functions in executable RAM.
 */
#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"

/** @def HAL_RAM_FUNC(name)
 * @brief Place a named function in RAM and prevent its inlining into flash.
 * @param name Function identifier, identical in declaration and definition.
 * @note Use as `static int HAL_RAM_FUNC(calculate)(int value) { ... }`.
 * RP uses Pico SDK's copied `.time_critical` sections; STM32 copies `.ram_func`
 * with `.data` at startup; ESP32 uses ESP-IDF's IRAM placement. Mock builds
 * keep normal host placement. Requires the target's supported linker/startup.
 * Only this function is placed: callees, constants and compiler runtime
 * helpers must be checked separately in the linked image. This does not make
 * a call safe while flash is unavailable, nor does it change thread safety.
 */
#if HAL_TARGET_IS_MOCK
#define HAL_RAM_FUNC(name) name
#elif !HAL_COMPILER_IS_GNU_LIKE
#error "HAL_RAM_FUNC requires a GNU-compatible firmware compiler"
#elif HAL_TARGET_IS_RP
#define HAL_RAM_FUNC(name)                                                     \
  __attribute__((section(".time_critical." #name), noinline)) name
#elif HAL_TARGET_IS_STM32G474
#define HAL_RAM_FUNC(name)                                                     \
  __attribute__((section(".ram_func." #name), noinline)) name
#elif HAL_TARGET_IS_ESP32_FAMILY
#define HAL_RAM_FUNC(name)                                                     \
  __attribute__((section(".iram1." #name), noinline)) name
#else
#error "HAL_RAM_FUNC has no mapping for this target"
#endif

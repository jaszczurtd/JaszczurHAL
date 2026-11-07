#pragma once

/** @file hal_memory.h
 * @brief Placement of functions and static data in backend-selected RAM.
 */
#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"

/** @def HAL_NOINIT
 * @brief Place an uninitialized static object outside startup clearing.
 * @note Use as `HAL_NOINIT static uint32_t retained;` without an initializer.
 * RP uses `.uninitialized_data`; STM32G474 and ESP32 use `.noinit`. Requires
 * the target's supported linker/startup. Validate the stored data before use:
 * cold boot and power loss leave its value unspecified.
 * Mock builds use ordinary static storage and do not emulate reset retention.
 * Placement does not provide synchronization.
 */
#if HAL_TARGET_IS_MOCK
#define HAL_NOINIT
#elif !HAL_COMPILER_IS_GNU_LIKE
#error "HAL_NOINIT requires a GNU-compatible firmware compiler"
#elif HAL_TARGET_IS_RP
#define HAL_NOINIT __attribute__((section(".uninitialized_data")))
#elif HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_ESP32_FAMILY
#define HAL_NOINIT __attribute__((section(".noinit")))
#else
#error "HAL_NOINIT has no mapping for this target"
#endif

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

/** @def HAL_CPU_ONLY_BSS(name)
 * @brief Place a zero-initialized static object in RAM selected for CPU access.
 * @param name Object identifier, used in its declaration or definition.
 * @note Use as `static uint8_t HAL_CPU_ONLY_BSS(buffer)[256];` at file scope.
 * The object keeps its type's alignment and is zeroed before constructors.
 * Omit its initializer or use an all-zero constant initializer; do not use
 * dynamic initialization. The caller must never pass this object's storage
 * to DMA. Ordinary SRAM is a valid backend mapping. STM32 builds select CCM
 * when TLS is disabled and SRAM when TLS needs CCM. RP and ESP32 use their
 * normal BSS; mock builds use host storage. Requires supported linker/startup.
 * Placement does not provide synchronization or make DMA access safe.
 */
#ifndef JH_CPU_ONLY_BSS_SECTION
#define JH_CPU_ONLY_BSS_SECTION ".bss.hal_cpu.sram"
#endif

#if HAL_COMPILER_IS_GNU_LIKE && !defined(__APPLE__)
#define HAL_CPU_ONLY_BSS(name)                                                 \
  __attribute__((section(JH_CPU_ONLY_BSS_SECTION "." #name))) name
#else
#define HAL_CPU_ONLY_BSS(name) name
#endif

#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system. */

/* ── Target (backend) selection ───────────────────────────────────────────
 * JaszczurHAL picks one hardware backend via a single switch.
 * To pin it explicitly,
 * uncomment exactly one:
 *
 *   #define HAL_TARGET_RP2040
 *   #define HAL_TARGET_STM32G474
 *   #define HAL_TARGET_MOCK
 *
 * See doc/HAL_FLAGS.txt / src/hal/hal_target.h for details.
 *
 * ── Module enable flags ───────────────────────────────────────────────────
 * Blink uses only core GPIO + debug serial, which are always available, so no
 * HAL_ENABLE_* flags are needed here.
 */

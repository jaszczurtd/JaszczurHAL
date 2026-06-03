#pragma once

/* ── Target (backend) selection ───────────────────────────────────────────
 * JaszczurHAL picks one hardware backend via a single switch. On arduino-pico
 * the RP2040 target is AUTO-DETECTED, so this example needs nothing here.
 *
 * To pin it explicitly (recommended for non-Arduino or multi-target projects),
 * uncomment exactly one:
 *
 *   #define HAL_TARGET_RP2040
 *   #define HAL_TARGET_STM32G474
 *   #define HAL_TARGET_MOCK
 *
 * See src/HAL_FLAGS.txt / src/hal/hal_target.h for details.
 *
 * ── Module enable flags ───────────────────────────────────────────────────
 * Blink uses only core GPIO + debug serial, which are always available, so no
 * HAL_ENABLE_* flags are needed here.
 */

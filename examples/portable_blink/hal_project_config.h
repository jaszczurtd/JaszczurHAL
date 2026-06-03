#pragma once

/* ── Target (backend) selection ───────────────────────────────────────────
 * On arduino-pico the RP2040 target is AUTO-DETECTED, so this sketch needs
 * nothing here. The STM32G474 build (g474/) selects HAL_TARGET_STM32G474 via
 * its CMake/build.sh. To pin a target explicitly, uncomment one:
 *
 *   #define HAL_TARGET_RP2040
 *   #define HAL_TARGET_STM32G474
 *   #define HAL_TARGET_MOCK
 *
 * See src/HAL_FLAGS.txt / src/hal/hal_target.h.
 *
 * ── Module enable flags ───────────────────────────────────────────────────
 * Blink uses only core GPIO + debug serial (always available), so no
 * HAL_ENABLE_* flags are needed.
 */

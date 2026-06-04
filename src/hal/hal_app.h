#pragma once

/**
 * @file hal_app.h
 * @brief Portable application entry-point contract.
 *
 * When the library is built with @c HAL_PROVIDE_APP_ENTRY defined, JaszczurHAL
 * supplies the platform-specific entry point (Arduino @c setup()/@c loop() or
 * bare-metal @c main()) and dispatches to the three user-defined functions
 * declared below. The client never writes @c main(), @c setup(), @c loop(),
 * or a @c .ino file - only portable application logic.
 *
 * ── Required ─────────────────────────────────────────────────────────────────
 *   @ref app_start  - one-time initialisation (called once before any task).
 *   @ref app_task0  - primary super-loop iteration (core 0 on RP2040).
 *
 * ── Optional ─────────────────────────────────────────────────────────────────
 *   @ref app_task1  - secondary loop (core 1 on RP2040; cooperative round-robin
 *                     with task0 on STM32 until FreeRTOS support lands).
 *                     Weak-linked: if not defined by the client, it is simply
 *                     not called (RP2040) or skipped (STM32/mock).
 *
 * ── Backend mapping ──────────────────────────────────────────────────────────
 *
 *   RP2040 (Arduino-pico):
 *       setup()  -> app_start()
 *       loop()   -> app_task0()
 *       loop1()  -> app_task1()        [core 1, true parallelism]
 *
 *   STM32G474 (bare-metal):
 *       main() { app_start(); for(;;) { app_task0(); app_task1(); } }
 *       NOTE: task1 runs cooperatively in the same loop as task0.
 *             This is a TEMPORARY solution pending FreeRTOS integration.
 *
 *   Mock / host:
 *       main() { app_start(); for(;;) { app_task0(); app_task1(); } }
 *       (useful for standalone host demo apps; unit tests provide their own
 *       main and should NOT define HAL_PROVIDE_APP_ENTRY.)
 *
 * ── How to enable ────────────────────────────────────────────────────────────
 *   Define @c HAL_PROVIDE_APP_ENTRY in your @c hal_project_config.h or pass
 *   @c -DHAL_PROVIDE_APP_ENTRY via the build system / build_*_lib.sh @c -D flag.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time application initialisation.
 *
 * Called once before any task function runs. Use this for pin setup, peripheral
 * init, serial begin, etc.
 */
void app_start(void);

/**
 * @brief Primary application loop iteration (core 0 on RP2040).
 *
 * Called repeatedly in an infinite loop. Must not block indefinitely.
 */
void app_task0(void);

/**
 * @brief Secondary application loop iteration (optional).
 *
 * On RP2040: runs on core 1 (true hardware parallelism via loop1()).
 * On STM32:  called cooperatively after app_task0() in the same super-loop
 *            (temporary; will become a FreeRTOS task).
 * On mock:   called after app_task0() in the same loop.
 *
 * Weak-linked - if not defined by the client, it is simply skipped.
 */
void app_task1(void);

#ifdef __cplusplus
}
#endif

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
 * -- Optional ----------------------------------------------------------------
 *   @ref app_task1  - secondary loop. It is dispatched only when
 *                     @c HAL_ENABLE_APP_TASK1 is defined. On RP2040 this emits
 *                     Arduino @c loop1(), which starts the core-1 path.
 *                     On STM32 FreeRTOS builds it runs as a second FreeRTOS
 *                     task. On STM32 non-FreeRTOS and mock builds it runs
 *                     cooperatively after task0. If enabled but not implemented
 *                     by the client, a weak empty default is linked.
 *
 * ── Backend mapping ──────────────────────────────────────────────────────────
 *
 *   RP2040 (Arduino-pico):
 *       setup()  -> app_start()
 *       loop()   -> app_task0()
 *       loop1()  -> app_task1()        [only with HAL_ENABLE_APP_TASK1]
 *
 *   STM32G474 (bare-metal):
 *       main() { app_start(); for(;;) { app_task0(); optional app_task1(); } }
 *       NOTE: task1 runs cooperatively in the same loop as task0.
 *
 *   STM32G474 (FreeRTOS):
 *       main() -> app_start()
 *              -> create app_task0 task
 *              -> create app_task1 task [only with HAL_ENABLE_APP_TASK1]
 *              -> vTaskStartScheduler()
 *       Task stack depths and priorities can be overridden with:
 *       HAL_FREERTOS_TASK0_STACK, HAL_FREERTOS_TASK1_STACK,
 *       HAL_FREERTOS_TASK0_PRIORITY, HAL_FREERTOS_TASK1_PRIORITY.
 *
 *   Mock / host:
 *       main() { app_start(); for(;;) { app_task0(); optional app_task1(); } }
 *       (useful for standalone host demo apps; unit tests provide their own
 *       main and should NOT define HAL_PROVIDE_APP_ENTRY.)
 *
 * ── How to enable ────────────────────────────────────────────────────────────
 *   Define @c HAL_PROVIDE_APP_ENTRY in your @c hal_project_config.h or pass
 *   @c -DHAL_PROVIDE_APP_ENTRY via the build system, for example through a
 *   @c scripts/build_*_lib.sh @c -D option. Define @c HAL_ENABLE_APP_TASK1
 *   only when the application intentionally uses @c app_task1. On RP2040 this
 *   is also the opt-in for @c loop1/core 1.
 */

#include <stdbool.h>
#include <stdint.h>

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
 * On RP2040: runs on core 1 (true hardware parallelism via loop1()) when
 *            @c HAL_ENABLE_APP_TASK1 is defined.
 * On STM32:  runs as a second FreeRTOS task when @c HAL_ENABLE_FREERTOS is
 *            defined, otherwise called cooperatively after app_task0() in the
 *            same super-loop.
 * On mock:   called after app_task0() in the same loop when enabled.
 *
 * Weak-linked - if enabled but not defined by the client, the default is empty.
 */
void app_task1(void);

#ifdef __cplusplus
}
#endif

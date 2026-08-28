#pragma once

/**
 * @file hal_app.h
 * @brief Portable application entry-point contract.
 *
 * When the library is built with @c HAL_PROVIDE_APP_ENTRY defined, JaszczurHAL
 * supplies @c main() and dispatches to the three user-defined functions
 * declared below. The client writes only portable application logic.
 *
 * ── Required ─────────────────────────────────────────────────────────────────
 *   @ref app_start  - one-time initialisation (called once before any task).
 *   @ref app_task0  - primary super-loop iteration (core 0 on RP targets).
 *
 * -- Optional ----------------------------------------------------------------
 *   @ref app_task1  - secondary loop. It is dispatched only when
 *                     @c HAL_ENABLE_APP_TASK1 is defined. On RP it starts the
 *                     core-1 path. On STM32 FreeRTOS builds it runs as a second
 *                     FreeRTOS task. On ESP32-S3 it defaults to core 1. On
 *                     STM32 bare-metal and mock builds it runs cooperatively
 *                     after task0. If enabled but not implemented by the
 *                     client, a weak empty default is linked.
 *
 * ── Backend mapping ──────────────────────────────────────────────────────────
 *
 *   RP family:
 *       bare: main() -> optional core-1 flash-safety bootstrap
 *                    -> app_start()
 *                    -> core 0 super-loop with app_task0()
 *                    -> optional core 1 super-loop with app_task1()
 *       FreeRTOS: main() -> app_start()
 *                         -> create app_task0 pinned to core 0
 *                         -> optional app_task1 pinned to core 1
 *                         -> vTaskStartScheduler()
 *       The bare core-1 path registers as a multicore-lockout victim before
 *       app_start(), then waits until application initialization completes.
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
 *   ESP32 family (ESP-IDF):
 *       app_main() -> app_start()
 *                  -> create app_task0 pinned to core 0 by default
 *                  -> create app_task1 pinned to core 1 by default
 *                     [only with HAL_ENABLE_APP_TASK1]
 *                  -> return to the already-running ESP-IDF scheduler.
 *       HAL_FREERTOS_TASK0_CORE / HAL_FREERTOS_TASK1_CORE may select a valid
 *       target core or -1 for no affinity. Stack and priority overrides use
 *       the same HAL_FREERTOS_TASK* macros as the other RTOS backends.
 *
 *   Mock / host:
 *       main() { app_start(); for(;;) { app_task0(); optional app_task1(); } }
 *       (useful for standalone host demo apps; unit tests provide their own
 *       main without HAL_PROVIDE_APP_ENTRY.)
 *
 * ── How to enable ────────────────────────────────────────────────────────────
 *   Define @c HAL_PROVIDE_APP_ENTRY in your @c hal_project_config.h or pass
 *   @c -DHAL_PROVIDE_APP_ENTRY via the build system, for example through a
 *   @c JH_EXTRA_DEFINES. Define @c HAL_ENABLE_APP_TASK1 when the application
 *   intentionally uses @c app_task1. On RP targets this also opts into core 1.
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
 * @brief Primary application loop iteration (core 0 on RP targets).
 *
 * Called repeatedly in an infinite loop. Must not block indefinitely.
 */
void app_task0(void);

/**
 * @brief Secondary application loop iteration (optional).
 *
 * On RP family: runs on core 1 when @c HAL_ENABLE_APP_TASK1 is defined.
 *               Pico SDK uses @c multicore_launch_core1() in bare mode or
 *               a core-affined FreeRTOS task in FreeRTOS mode.
 * On STM32:  runs as a second FreeRTOS task when @c HAL_ENABLE_FREERTOS is
 *            defined, otherwise called cooperatively after app_task0() in the
 *            same super-loop.
 * On ESP32-S3: runs as an ESP-IDF FreeRTOS task pinned to core 1 by default.
 * On mock:   called after app_task0() in the same loop when enabled.
 *
 * Weak-linked - if enabled but not defined by the client, the default is empty.
 */
void app_task1(void);

#ifdef __cplusplus
}
#endif

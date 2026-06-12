/**
 * @file hal_app_entry.cpp
 * @brief Platform entry-point shim - bridges app_start/app_task0/app_task1
 *        to the backend-specific entry mechanism.
 *
 * Compiled into libJaszczurHAL.a on all backends but emits code ONLY when
 * HAL_PROVIDE_APP_ENTRY is defined. Without the flag, this translation unit
 * produces zero symbols, so existing projects with their own setup()/loop()
 * or main() are unaffected.
 *
 * See src/hal/hal_app.h for the full contract and backend mapping.
 */

#include "hal/hal_config.h"

#if defined(HAL_PROVIDE_APP_ENTRY)

#include "hal/hal_app.h"

/* -- Weak default for app_task1 ----------------------------------------------
 * If the client enables HAL_ENABLE_APP_TASK1 but does not define app_task1(),
 * the weak stub below is linked instead and does nothing.
 */
extern "C" __attribute__((weak)) void app_task1(void) {
  /* intentionally empty */
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * RP2040 / Arduino-pico backend
 *
 * The Arduino core provides main() (in cores/rp2040/main.cpp) which calls
 * setup() once, then loop() in an infinite loop on core 0. If loop1() is
 * defined, arduino-pico starts the core-1 path, so HAL emits loop1() only
 * when the application explicitly defines HAL_ENABLE_APP_TASK1.
 * ═══════════════════════════════════════════════════════════════════════════════
 */
#if HAL_TARGET_IS_RP2040

#include <Arduino.h>

void setup(void) { app_start(); }

void loop(void) { app_task0(); }

#ifdef HAL_ENABLE_APP_TASK1
void loop1(void) { app_task1(); }
#endif

/* ═══════════════════════════════════════════════════════════════════════════════
 * STM32G474 backend
 *
 * Bare-metal builds keep the original cooperative super-loop. FreeRTOS builds
 * call app_start(), create task0 and optional task1, then start the scheduler.
 * ═══════════════════════════════════════════════════════════════════════════════
 */
#elif HAL_TARGET_IS_STM32G474

#if defined(HAL_ENABLE_FREERTOS)

#include <FreeRTOS.h>
#include <task.h>

#ifndef HAL_FREERTOS_TASK0_STACK
#define HAL_FREERTOS_TASK0_STACK 512u
#endif

#ifndef HAL_FREERTOS_TASK1_STACK
#define HAL_FREERTOS_TASK1_STACK 512u
#endif

#ifndef HAL_FREERTOS_TASK0_PRIORITY
#define HAL_FREERTOS_TASK0_PRIORITY (tskIDLE_PRIORITY + 1u)
#endif

#ifndef HAL_FREERTOS_TASK1_PRIORITY
#define HAL_FREERTOS_TASK1_PRIORITY (tskIDLE_PRIORITY + 1u)
#endif

static void hal_freertos_app_task0(void *arg) {
  (void)arg;

  for (;;) {
    app_task0();
  }
}

#ifdef HAL_ENABLE_APP_TASK1
static void hal_freertos_app_task1(void *arg) {
  (void)arg;

  for (;;) {
    app_task1();
  }
}
#endif

int main(void) {
  app_start();

  BaseType_t created =
      xTaskCreate(hal_freertos_app_task0, "jh_app0",
                  (configSTACK_DEPTH_TYPE)HAL_FREERTOS_TASK0_STACK, nullptr,
                  (UBaseType_t)HAL_FREERTOS_TASK0_PRIORITY, nullptr);
  HAL_ASSERT(created == pdPASS, "hal_app_entry: xTaskCreate app_task0 failed");

#ifdef HAL_ENABLE_APP_TASK1
  created =
      xTaskCreate(hal_freertos_app_task1, "jh_app1",
                  (configSTACK_DEPTH_TYPE)HAL_FREERTOS_TASK1_STACK, nullptr,
                  (UBaseType_t)HAL_FREERTOS_TASK1_PRIORITY, nullptr);
  HAL_ASSERT(created == pdPASS, "hal_app_entry: xTaskCreate app_task1 failed");
#endif

  vTaskStartScheduler();
  HAL_ASSERT(false, "hal_app_entry: vTaskStartScheduler returned");

  for (;;) {
  }
}

#else

int main(void) {
  app_start();

  for (;;) {
    app_task0();
#ifdef HAL_ENABLE_APP_TASK1
    app_task1(); /* cooperative - same loop, no preemption */
#endif
  }
}

#endif /* HAL_ENABLE_FREERTOS */

/* ═══════════════════════════════════════════════════════════════════════════════
 * Mock / host backend
 *
 * Useful for standalone host demo applications. Unit tests should NOT define
 * HAL_PROVIDE_APP_ENTRY - they supply their own main().
 * ═══════════════════════════════════════════════════════════════════════════════
 */
#elif HAL_TARGET_IS_MOCK

int main(void) {
  app_start();

  for (;;) {
    app_task0();
#ifdef HAL_ENABLE_APP_TASK1
    app_task1();
#endif
  }
}

#endif /* HAL_TARGET selection */

#endif /* HAL_PROVIDE_APP_ENTRY */

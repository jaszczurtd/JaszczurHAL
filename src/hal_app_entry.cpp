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
 * STM32G474 bare-metal backend
 *
 * No RTOS yet. With HAL_ENABLE_APP_TASK1, task1 runs cooperatively after task0.
 * TODO: Once FreeRTOS is integrated, app_task1() should be spawned as a
 *       separate FreeRTOS task with its own stack and priority.
 * ═══════════════════════════════════════════════════════════════════════════════
 */
#elif HAL_TARGET_IS_STM32G474

int main(void) {
  app_start();

  for (;;) {
    app_task0();
#ifdef HAL_ENABLE_APP_TASK1
    app_task1(); /* cooperative - same loop, no preemption */
#endif
  }
}

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

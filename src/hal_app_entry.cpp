/**
 * @file hal_app_entry.cpp
 * @brief Platform entry-point shim - bridges app_start/app_task0/app_task1
 *        to the backend-specific entry mechanism.
 *
 * Compiled into libJaszczurHAL.a on all backends but emits code only when
 * HAL_PROVIDE_APP_ENTRY is defined. Without the flag, this translation unit
 * produces zero symbols, so existing projects with their own main() are
 * unaffected.
 *
 * See src/hal/core/hal_app.h for the full contract and backend mapping.
 */

#include "hal/core/hal_config.h"

#if defined(HAL_PROVIDE_APP_ENTRY)

#include "hal/core/hal_app.h"
#include "hal/system/hal_system.h"

/* -- Weak default for app_task1 ----------------------------------------------
 * If the client enables HAL_ENABLE_APP_TASK1 but does not define app_task1(),
 * the weak stub below is linked instead and does nothing.
 */
extern "C" __attribute__((weak)) void app_task1(void) {
  /* intentionally empty */
}

#if HAL_TARGET_IS_RP

#include "hal/impl/rp2040/drivers/flash/rp_flash_transaction.h"
#include "hal/usb/hal_usb.h"

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

static void hal_rp_freertos_app_task0(void *arg) {
  (void)arg;

  for (;;) {
    app_task0();
  }
}

#ifdef HAL_ENABLE_APP_TASK1
static void hal_rp_freertos_app_task1(void *arg) {
  (void)arg;

  for (;;) {
    app_task1();
  }
}
#endif

int main(void) {
  hal_fault_subsystem_init();
  const hal_status_t flash_status = jh_rp_flash_transaction_core_init();
  HAL_ASSERT(flash_status == HAL_OK,
             "hal_app_entry: flash coordinator init failed");
  (void)hal_usb_init();
  app_start();

#if configNUMBER_OF_CORES > 1
  BaseType_t created = xTaskCreateAffinitySet(
      hal_rp_freertos_app_task0, "jh_app0",
      (configSTACK_DEPTH_TYPE)HAL_FREERTOS_TASK0_STACK, nullptr,
      (UBaseType_t)HAL_FREERTOS_TASK0_PRIORITY, 1u << 0u, nullptr);
#else
  BaseType_t created =
      xTaskCreate(hal_rp_freertos_app_task0, "jh_app0",
                  (configSTACK_DEPTH_TYPE)HAL_FREERTOS_TASK0_STACK, nullptr,
                  (UBaseType_t)HAL_FREERTOS_TASK0_PRIORITY, nullptr);
#endif
  HAL_ASSERT(created == pdPASS, "hal_app_entry: xTaskCreate app_task0 failed");

#ifdef HAL_ENABLE_APP_TASK1
#if configNUMBER_OF_CORES == 1
#error "HAL_ENABLE_APP_TASK1 requires HAL_FREERTOS_CORE_COUNT=2"
#endif
  created = xTaskCreateAffinitySet(
      hal_rp_freertos_app_task1, "jh_app1",
      (configSTACK_DEPTH_TYPE)HAL_FREERTOS_TASK1_STACK, nullptr,
      (UBaseType_t)HAL_FREERTOS_TASK1_PRIORITY, 1u << 1u, nullptr);
  HAL_ASSERT(created == pdPASS, "hal_app_entry: xTaskCreate app_task1 failed");
#endif

  vTaskStartScheduler();
  HAL_ASSERT(false, "hal_app_entry: vTaskStartScheduler returned");

  for (;;) {
  }
}

#else

#include <pico/multicore.h>

#ifdef HAL_ENABLE_APP_TASK1
static void hal_rp_native_core1_entry(void) {
  const hal_status_t flash_status = jh_rp_flash_transaction_core_init();
  HAL_ASSERT(flash_status == HAL_OK,
             "hal_app_entry: core1 flash coordinator init failed");

  for (;;) {
    app_task1();
  }
}
#endif

int main(void) {
  hal_fault_subsystem_init();
  const hal_status_t flash_status = jh_rp_flash_transaction_core_init();
  HAL_ASSERT(flash_status == HAL_OK,
             "hal_app_entry: flash coordinator init failed");
  (void)hal_usb_init();
  app_start();

#ifdef HAL_ENABLE_APP_TASK1
  multicore_launch_core1(hal_rp_native_core1_entry);
#endif

  for (;;) {
    app_task0();
  }
}

#endif /* HAL_ENABLE_FREERTOS */

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
#define HAL_FREERTOS_TASK0_STACK 768u
#endif

#ifndef HAL_FREERTOS_TASK1_STACK
#define HAL_FREERTOS_TASK1_STACK 768u
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
  hal_fault_subsystem_init();
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
  hal_fault_subsystem_init();
  app_start();

  for (;;) {
    app_task0();
#ifdef HAL_ENABLE_APP_TASK1
    app_task1(); /* cooperative - same loop, no preemption */
#endif
  }
}

#endif /* HAL_ENABLE_FREERTOS */

/* ESP-IDF starts the FreeRTOS scheduler before calling app_main(). */
#elif HAL_TARGET_IS_ESP32_FAMILY

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef HAL_FREERTOS_TASK0_STACK
#define HAL_FREERTOS_TASK0_STACK 3072u
#endif

#ifndef HAL_FREERTOS_TASK1_STACK
#define HAL_FREERTOS_TASK1_STACK 3072u
#endif

#ifndef HAL_FREERTOS_TASK0_PRIORITY
#define HAL_FREERTOS_TASK0_PRIORITY (tskIDLE_PRIORITY + 1u)
#endif

#ifndef HAL_FREERTOS_TASK1_PRIORITY
#define HAL_FREERTOS_TASK1_PRIORITY (tskIDLE_PRIORITY + 1u)
#endif

#ifndef HAL_FREERTOS_TASK0_CORE
#define HAL_FREERTOS_TASK0_CORE 0
#endif

#ifndef HAL_FREERTOS_TASK1_CORE
#if HAL_TARGET_CPU_CORES > 1
#define HAL_FREERTOS_TASK1_CORE 1
#else
#define HAL_FREERTOS_TASK1_CORE 0
#endif
#endif

#if HAL_FREERTOS_TASK0_CORE < -1 ||                                            \
    HAL_FREERTOS_TASK0_CORE >= HAL_TARGET_CPU_CORES
#error "HAL_FREERTOS_TASK0_CORE must be -1 or name an existing target core"
#endif

#if HAL_FREERTOS_TASK1_CORE < -1 ||                                            \
    HAL_FREERTOS_TASK1_CORE >= HAL_TARGET_CPU_CORES
#error "HAL_FREERTOS_TASK1_CORE must be -1 or name an existing target core"
#endif

static constexpr BaseType_t hal_esp32_task_core(int configured_core) {
  return configured_core == -1 ? (BaseType_t)tskNO_AFFINITY
                               : (BaseType_t)configured_core;
}

static void hal_esp32_app_task0(void *arg) {
  (void)arg;
  for (;;) {
    app_task0();
  }
}

#ifdef HAL_ENABLE_APP_TASK1
static void hal_esp32_app_task1(void *arg) {
  (void)arg;
  for (;;) {
    app_task1();
  }
}
#endif

extern "C" void app_main(void) {
  hal_fault_subsystem_init();
  app_start();

  BaseType_t created = xTaskCreatePinnedToCore(
      hal_esp32_app_task0, "jh_app0",
      (configSTACK_DEPTH_TYPE)HAL_FREERTOS_TASK0_STACK, nullptr,
      (UBaseType_t)HAL_FREERTOS_TASK0_PRIORITY, nullptr,
      hal_esp32_task_core(HAL_FREERTOS_TASK0_CORE));
  configASSERT(created == pdPASS);

#ifdef HAL_ENABLE_APP_TASK1
  created = xTaskCreatePinnedToCore(
      hal_esp32_app_task1, "jh_app1",
      (configSTACK_DEPTH_TYPE)HAL_FREERTOS_TASK1_STACK, nullptr,
      (UBaseType_t)HAL_FREERTOS_TASK1_PRIORITY, nullptr,
      hal_esp32_task_core(HAL_FREERTOS_TASK1_CORE));
  configASSERT(created == pdPASS);
#endif
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
  hal_fault_subsystem_init();
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

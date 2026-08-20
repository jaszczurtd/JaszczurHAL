#pragma once

/*
 * FreeRTOSConfig.h for the STM32G474 backend.
 *
 * This is intentionally target-local: the kernel is a private backend
 * dependency, while applications use native FreeRTOS headers directly.
 */

#include <stddef.h>
#include <stdint.h>

#include "../port/stm32g474_clock.h"

#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE 0
#define configCPU_CLOCK_HZ ((uint32_t)JH_G474_CORE_CLOCK_HZ)
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES 8
#define configMINIMAL_STACK_SIZE ((uint16_t)128)
#ifndef HAL_FREERTOS_HEAP_SIZE
#define HAL_FREERTOS_HEAP_SIZE (24u * 1024u)
#endif
#define configTOTAL_HEAP_SIZE ((size_t)HAL_FREERTOS_HEAP_SIZE)
#define configMAX_TASK_NAME_LEN 16
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1

#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_QUEUE_SETS 0
#define configUSE_TASK_NOTIFICATIONS 1

#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 2
#define configTIMER_QUEUE_LENGTH 8
#define configTIMER_TASK_STACK_DEPTH 256

#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configUSE_MALLOC_FAILED_HOOK 1
#ifdef HAL_ENABLE_STACK_GUARD
#define configCHECK_FOR_STACK_OVERFLOW 2
#else
#define configCHECK_FOR_STACK_OVERFLOW 0
#endif

#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

#define configUSE_TRACE_FACILITY 0
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configUSE_NEWLIB_REENTRANT 0

#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xResumeFromISR 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

/* STM32G474 implements 4 NVIC priority bits. */
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY                                        \
  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY                                   \
  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Let the Cortex-M4F port own the exception handlers by their vector names. */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* Synchronize the HAL's 64-bit epoch after the kernel has committed its tick.
 * Deferred ticks are therefore counted during xTaskResumeAll(), not once when
 * pended and again when replayed. The application tick-hook remains free. */
#ifdef __cplusplus
extern "C" {
#endif
void stm32g474_freertos_tick_sync(uint32_t tick_count);
#ifdef __cplusplus
}
#endif
#define traceRETURN_xTaskIncrementTick(xSwitchRequired)                        \
  do {                                                                         \
    (void)(xSwitchRequired);                                                   \
    stm32g474_freertos_tick_sync((uint32_t)xTickCount);                        \
  } while (0)

#define configASSERT(x)                                                        \
  do {                                                                         \
    if ((x) == 0) {                                                            \
      taskDISABLE_INTERRUPTS();                                                \
      for (;;) {                                                               \
      }                                                                        \
    }                                                                          \
  } while (0)

#pragma once

#include <hardware/platform_defs.h>
#include <stddef.h>
#include <stdint.h>

#ifndef HAL_FREERTOS_CORE_COUNT
#define HAL_FREERTOS_CORE_COUNT 2
#endif
#if HAL_FREERTOS_CORE_COUNT != 1 && HAL_FREERTOS_CORE_COUNT != 2
#error "HAL_FREERTOS_CORE_COUNT must be 1 or 2"
#endif

#define configNUMBER_OF_CORES HAL_FREERTOS_CORE_COUNT
#define configUSE_CORE_AFFINITY (HAL_FREERTOS_CORE_COUNT > 1)
#define configRUN_MULTIPLE_PRIORITIES 1

#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_TICKLESS_IDLE 0
#define configCPU_CLOCK_HZ ((uint32_t)SYS_CLK_HZ)
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES 8
#define configMINIMAL_STACK_SIZE ((configSTACK_DEPTH_TYPE)256)
#define configMAX_TASK_NAME_LEN 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1

#ifndef HAL_FREERTOS_HEAP_SIZE
#define HAL_FREERTOS_HEAP_SIZE (164u * 1024u)
#endif
#define configTOTAL_HEAP_SIZE ((size_t)HAL_FREERTOS_HEAP_SIZE)

#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_QUEUE_SETS 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1

#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 2
#define configTIMER_QUEUE_LENGTH 8
#define configTIMER_TASK_STACK_DEPTH 384

#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configUSE_MALLOC_FAILED_HOOK 1
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configSTACK_DEPTH_TYPE uint32_t

#define configUSE_IDLE_HOOK 0
#define configUSE_PASSIVE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

#define configUSE_TRACE_FACILITY 0
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configUSE_NEWLIB_REENTRANT 0

#define INCLUDE_eTaskGetState 1
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
#define INCLUDE_xTimerPendFunctionCall 1

#define configENABLE_MPU 0
#define configENABLE_TRUSTZONE 0
#define configRUN_FREERTOS_SECURE_ONLY 1
#define configENABLE_FPU 1
#define configUSE_DYNAMIC_EXCEPTION_HANDLERS 0

#if defined(PICO_RP2350)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 16
#endif

#define configSUPPORT_PICO_SYNC_INTEROP 1
#define configSUPPORT_PICO_TIME_INTEROP 1
#define configTICK_CORE 0

#ifdef __cplusplus
extern "C" {
#endif

void jh_rp_freertos_assert_fail(const char *file, int line);

#ifdef __cplusplus
}
#endif

#define configASSERT(condition)                                                \
  do {                                                                         \
    if (!(condition)) {                                                        \
      jh_rp_freertos_assert_fail(__FILE__, __LINE__);                          \
    }                                                                          \
  } while (0)

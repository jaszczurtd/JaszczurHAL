#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474 && defined(HAL_ENABLE_FREERTOS)

#include <FreeRTOS.h>
#include <task.h>

void vApplicationMallocFailedHook(void) {
  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
  (void)task;
  (void)task_name;

  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

#endif

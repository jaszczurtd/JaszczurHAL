#include "hal/hal_sync.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"
#include "utils/SmartTimers.h"
#include "utils/unity.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <stddef.h>
#include <stdint.h>

namespace {

constexpr int kWorkerCount = 2;
constexpr int kWorkerIterations = 80;
constexpr int kOnceWorkerCount = 4;

hal_mutex_t s_counter_mutex;
hal_mutex_t s_once_mutex;
SemaphoreHandle_t s_done_sem;
SmartTimers *s_timer;

volatile int s_failures;
volatile int s_shared_counter;
volatile int s_once_hits;
volatile int s_timer_callbacks;
volatile int s_task_delay_tick_delta;
volatile int s_idle_calls;

void record_failure(void) {
  __atomic_fetch_add(&s_failures, 1, __ATOMIC_RELAXED);
}

void timer_callback(void) {
  __atomic_fetch_add(&s_timer_callbacks, 1, __ATOMIC_RELAXED);
}

void give_done(void) {
  if (s_done_sem != NULL) {
    (void)xSemaphoreGive(s_done_sem);
  }
}

void counter_worker(void *arg) {
  (void)arg;

  for (int i = 0; i < kWorkerIterations; ++i) {
    hal_mutex_lock(s_counter_mutex);
    int value = s_shared_counter;
    value++;
    s_shared_counter = value;
    hal_mutex_unlock(s_counter_mutex);

    s_timer->tick();
    hal_idle();
    __atomic_fetch_add(&s_idle_calls, 1, __ATOMIC_RELAXED);
    hal_delay_ms(1);
  }

  give_done();
  vTaskDelete(NULL);
}

void once_worker(void *arg) {
  (void)arg;

  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_once_mutex);
  if (mutex == NULL) {
    record_failure();
  } else {
    hal_mutex_lock(mutex);
    __atomic_fetch_add(&s_once_hits, 1, __ATOMIC_RELAXED);
    hal_mutex_unlock(mutex);
  }

  give_done();
  vTaskDelete(NULL);
}

void wait_for_done(int expected_count) {
  for (int i = 0; i < expected_count; ++i) {
    if (xSemaphoreTake(s_done_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
      record_failure();
      return;
    }
  }
}

void supervisor_task(void *arg) {
  (void)arg;

  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
    record_failure();
  }

  const TickType_t before_delay = xTaskGetTickCount();
  hal_delay_ms(3);
  const TickType_t after_delay = xTaskGetTickCount();
  s_task_delay_tick_delta = (int)(after_delay - before_delay);
  if (s_task_delay_tick_delta < 3) {
    record_failure();
  }

  s_counter_mutex = hal_mutex_create();
  if (s_counter_mutex == NULL) {
    record_failure();
    vTaskEndScheduler();
    vTaskDelete(NULL);
  }

  s_done_sem = xSemaphoreCreateCounting(kOnceWorkerCount, 0);
  if (s_done_sem == NULL) {
    record_failure();
    vTaskEndScheduler();
    vTaskDelete(NULL);
  }

  for (int i = 0; i < kOnceWorkerCount; ++i) {
    if (xTaskCreate(once_worker, "once", 512, NULL, 3, NULL) != pdPASS) {
      record_failure();
    }
  }
  wait_for_done(kOnceWorkerCount);

  if (s_once_hits != kOnceWorkerCount || s_once_mutex == NULL) {
    record_failure();
  }

  vSemaphoreDelete(s_done_sem);
  s_done_sem = xSemaphoreCreateCounting(kWorkerCount, 0);
  if (s_done_sem == NULL) {
    record_failure();
    vTaskEndScheduler();
    vTaskDelete(NULL);
  }

  s_timer = new SmartTimers();
  if (s_timer == NULL) {
    record_failure();
    vTaskEndScheduler();
    vTaskDelete(NULL);
  }
  s_timer->begin(timer_callback, 2);

  for (int i = 0; i < kWorkerCount; ++i) {
    if (xTaskCreate(counter_worker, "worker", 512, NULL, 3, NULL) != pdPASS) {
      record_failure();
    }
  }
  wait_for_done(kWorkerCount);

  if (s_shared_counter != (kWorkerCount * kWorkerIterations)) {
    record_failure();
  }
  if (s_timer_callbacks <= 0) {
    record_failure();
  }
  if (s_idle_calls != (kWorkerCount * kWorkerIterations)) {
    record_failure();
  }

  delete s_timer;
  s_timer = NULL;

  vSemaphoreDelete(s_done_sem);
  s_done_sem = NULL;

  hal_mutex_destroy(s_once_mutex);
  s_once_mutex = NULL;

  hal_mutex_destroy(s_counter_mutex);
  s_counter_mutex = NULL;

  vTaskEndScheduler();
  vTaskDelete(NULL);
}

} // namespace

void setUp(void) {
  s_counter_mutex = NULL;
  s_once_mutex = NULL;
  s_done_sem = NULL;
  s_timer = NULL;
  s_failures = 0;
  s_shared_counter = 0;
  s_once_hits = 0;
  s_timer_callbacks = 0;
  s_task_delay_tick_delta = 0;
  s_idle_calls = 0;
}

void tearDown(void) {}

void test_hal_freertos_posix_scheduler_runtime(void) {
  BaseType_t created =
      xTaskCreate(supervisor_task, "supervisor", 1024, NULL, 4, NULL);
  TEST_ASSERT_EQUAL_INT(pdPASS, created);

  vTaskStartScheduler();

  TEST_ASSERT_EQUAL_INT(0, s_failures);
  TEST_ASSERT_EQUAL_INT(kOnceWorkerCount, s_once_hits);
  TEST_ASSERT_EQUAL_INT(kWorkerCount * kWorkerIterations, s_shared_counter);
  TEST_ASSERT_GREATER_THAN_INT(0, s_timer_callbacks);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(3, s_task_delay_tick_delta);
  TEST_ASSERT_EQUAL_INT(kWorkerCount * kWorkerIterations, s_idle_calls);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_hal_freertos_posix_scheduler_runtime);
  return UNITY_END();
}

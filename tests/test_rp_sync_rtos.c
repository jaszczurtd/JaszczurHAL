#include <hal/system/hal_sync.h>
#include <pico/platform.h>
#include <semphr.h>
#include <setjmp.h>
#include <stdlib.h>
#include <task.h>
#include <utils/unity.h>

static unsigned s_token, s_takes, s_queries, s_gives;
static bool s_firstAvailable, s_isr;
static BaseType_t s_scheduler;
static TickType_t s_waits[2];
static hal_mutex_t s_mutex;
static jmp_buf s_assertJump;
static bool s_assertExpected;

void hal_assert_fail(const char *message) {
  if (s_assertExpected) {
    longjmp(s_assertJump, 1);
  }
  UnityFail(message, __LINE__);
  abort();
}
bool jh_test_in_isr(void) { return s_isr; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_token; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
  TEST_ASSERT_EQUAL_PTR(&s_token, mutex);
  TEST_ASSERT_LESS_THAN_UINT(2u, s_takes);
  s_waits[s_takes++] = wait;
  return s_takes == 1u ? (BaseType_t)s_firstAvailable : pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
  TEST_ASSERT_EQUAL_PTR(&s_token, mutex);
  ++s_gives;
  return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t mutex) {
  TEST_ASSERT_EQUAL_PTR(&s_token, mutex);
}
BaseType_t xTaskGetSchedulerState(void) {
  ++s_queries;
  return s_scheduler;
}
uint get_core_num(void) { return 0u; }
uint32_t save_and_disable_interrupts(void) { return 0u; }
void restore_interrupts(uint32_t state) { (void)state; }
void setUp(void) {
  s_takes = 0u;
  s_queries = 0u;
  s_gives = 0u;
  s_firstAvailable = true;
  s_isr = false;
  s_assertExpected = false;
  s_scheduler = taskSCHEDULER_RUNNING;
  s_mutex = hal_mutex_create();
  TEST_ASSERT_NOT_NULL(s_mutex);
}
void tearDown(void) { hal_mutex_destroy(s_mutex); }
static void test_uncontended_mutex_avoids_scheduler_lock(void) {
  hal_mutex_lock(s_mutex);
  TEST_ASSERT_EQUAL_UINT(1u, s_takes);
  TEST_ASSERT_EQUAL_UINT32(0u, s_waits[0]);
  TEST_ASSERT_EQUAL_UINT(0u, s_queries);
  hal_mutex_unlock(s_mutex);
  TEST_ASSERT_EQUAL_UINT(1u, s_gives);
}
static void test_contended_mutex_keeps_wait_policy(void) {
  s_firstAvailable = false;
  hal_mutex_lock(s_mutex);
  TEST_ASSERT_EQUAL_UINT(2u, s_takes);
  TEST_ASSERT_EQUAL_UINT32(portMAX_DELAY, s_waits[1]);
  TEST_ASSERT_EQUAL_UINT(1u, s_queries);
  hal_mutex_unlock(s_mutex);
  s_takes = 0u;
  s_queries = 0u;
  s_scheduler = 0;
  hal_mutex_lock(s_mutex);
  TEST_ASSERT_EQUAL_UINT(2u, s_takes);
  TEST_ASSERT_EQUAL_UINT32(0u, s_waits[0]);
  TEST_ASSERT_EQUAL_UINT32(0u, s_waits[1]);
  TEST_ASSERT_EQUAL_UINT(1u, s_queries);
  hal_mutex_unlock(s_mutex);
}
static void test_isr_cannot_take_task_mutex(void) {
  s_isr = true;
  s_assertExpected = true;
  if (setjmp(s_assertJump) == 0) {
    hal_mutex_lock(s_mutex);
    TEST_FAIL_MESSAGE("Task mutex accepted an ISR caller");
  }
  s_assertExpected = false;
  TEST_ASSERT_EQUAL_UINT(0u, s_takes);
  TEST_ASSERT_EQUAL_UINT(0u, s_queries);
  TEST_ASSERT_FALSE(hal_mutex_try_lock(s_mutex));
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_uncontended_mutex_avoids_scheduler_lock);
  RUN_TEST(test_contended_mutex_keeps_wait_policy);
  RUN_TEST(test_isr_cannot_take_task_mutex);
  return UNITY_END();
}

#include <hal/system/hal_sync.h>
#include <hal/timers/hal_timer.h>
#include <pico/time.h>
#include <utils/unity.h>

struct alarm_pool {
  unsigned token;
};
struct hal_mutex_impl_t {
  unsigned token;
};
static struct alarm_pool s_pool;
static struct hal_mutex_impl_t s_mutex;
static bool s_masked, s_pending, s_remote;
static unsigned s_calls, s_retries, s_cancels;
static alarm_id_t s_id, s_failure;
static alarm_callback_t s_callback;
static void *s_user;

static void fire_pending(void) {
  if (s_pending) {
    s_pending = false;
    if (s_callback(s_id, s_user) > 0) {
      ++s_retries;
      s_pending = true;
    }
  }
}
uint32_t save_and_disable_interrupts(void) {
  const uint32_t previous = s_masked ? 1u : 0u;
  s_masked = true;
  return previous;
}
void restore_interrupts(uint32_t state) {
  s_masked = state != 0u;
  if (!s_masked) {
    fire_pending();
  }
}
unsigned int __get_current_exception(void) { return 0u; }
void tight_loop_contents(void) {}
hal_mutex_t jh_hal_mutex_try_create(void) { return &s_mutex; }
hal_mutex_t hal_mutex_create(void) { return &s_mutex; }
void hal_mutex_lock(hal_mutex_t mutex) { (void)mutex; }
void hal_mutex_unlock(hal_mutex_t mutex) { (void)mutex; }
void hal_mutex_destroy(hal_mutex_t mutex) { (void)mutex; }
bool hardware_alarm_is_claimed(uint alarm) {
  (void)alarm;
  return false;
}
int hardware_alarm_claim_unused(bool required) {
  (void)required;
  return 0;
}
void hardware_alarm_unclaim(uint alarm) { (void)alarm; }
alarm_pool_t *alarm_pool_get_default(void) { return &s_pool; }
alarm_pool_t *alarm_pool_create(uint alarm, uint count) {
  (void)alarm;
  (void)count;
  return &s_pool;
}
alarm_pool_t *alarm_pool_create_with_unused_hardware_alarm(uint count) {
  return alarm_pool_create(0u, count);
}
void alarm_pool_destroy(alarm_pool_t *pool) { (void)pool; }
alarm_id_t alarm_pool_add_alarm_in_us(alarm_pool_t *pool, uint64_t delay,
                                      alarm_callback_t callback, void *user,
                                      bool fire_if_past) {
  (void)pool;
  (void)delay;
  (void)fire_if_past;
  if (s_failure != 0) {
    return s_failure;
  }
  s_callback = callback;
  s_user = user;
  ++s_id;
  s_pending = true;
  if (!s_masked || s_remote) {
    fire_pending();
  }
  return s_id;
}
bool alarm_pool_cancel_alarm(alarm_pool_t *pool, alarm_id_t id) {
  (void)pool;
  (void)id;
  ++s_cancels;
  s_pending = false;
  return true;
}
static int64_t on_alarm(hal_alarm_id_t id, void *user) {
  TEST_ASSERT_EQUAL_INT(s_id, id);
  TEST_ASSERT_EQUAL_PTR(&s_pool, user);
  ++s_calls;
  return 0;
}
void setUp(void) {
  s_masked = false;
  s_pending = false;
  s_remote = false;
  s_calls = 0u;
  s_retries = 0u;
  s_cancels = 0u;
  s_failure = 0;
}
void tearDown(void) {}
static void test_short_alarm_does_not_preempt_its_publisher(void) {
  hal_timer_result_t result;
  TEST_ASSERT_NOT_EQUAL(
      HAL_ALARM_INVALID,
      hal_timer_add_alarm_us_ex(1u, on_alarm, &s_pool, true, &result));
  fire_pending();
  TEST_ASSERT_EQUAL_UINT(0u, s_retries);
  TEST_ASSERT_EQUAL_UINT(1u, s_calls);
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, result);
  TEST_ASSERT_FALSE(s_masked);
}
static void test_nested_mask_and_other_core_publication(void) {
  s_masked = true;
  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID,
                        hal_timer_add_alarm_us(1u, on_alarm, &s_pool, true));
  TEST_ASSERT_TRUE(s_masked);
  TEST_ASSERT_EQUAL_UINT(0u, s_calls);
  restore_interrupts(0u);
  TEST_ASSERT_EQUAL_UINT(1u, s_calls);
  s_remote = true;
  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID,
                        hal_timer_add_alarm_us(1u, on_alarm, &s_pool, true));
  fire_pending();
  TEST_ASSERT_EQUAL_UINT(1u, s_retries);
  TEST_ASSERT_EQUAL_UINT(2u, s_calls);
}
static void test_failure_restores_irq_state_and_publication(void) {
  hal_timer_result_t result;
  s_failure = -1;
  TEST_ASSERT_EQUAL_INT(
      HAL_ALARM_INVALID,
      hal_timer_add_alarm_us_ex(1u, on_alarm, &s_pool, true, &result));
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_ERR_POOL_FULL, result);
  TEST_ASSERT_FALSE(s_masked);
  s_failure = 4 << 16;
  s_masked = true;
  TEST_ASSERT_EQUAL_INT(
      HAL_ALARM_INVALID,
      hal_timer_add_alarm_us_ex(1u, on_alarm, &s_pool, true, &result));
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_ERR_INTERNAL, result);
  TEST_ASSERT_EQUAL_UINT(1u, s_cancels);
  TEST_ASSERT_TRUE(s_masked);
  s_failure = 0;
  s_masked = false;
  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID,
                        hal_timer_add_alarm_us(1u, on_alarm, &s_pool, true));
  TEST_ASSERT_EQUAL_UINT(1u, s_calls);
  TEST_ASSERT_EQUAL_UINT(0u, s_retries);
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_short_alarm_does_not_preempt_its_publisher);
  RUN_TEST(test_nested_mask_and_other_core_publication);
  RUN_TEST(test_failure_restores_irq_state_and_publication);
  return UNITY_END();
}

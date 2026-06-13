#include "hal/hal_system.h"
#include "hal/hal_timer.h"
#include "utils/unity.h"

extern "C" void hal_stm32g474_timer_test_reset(void);
extern "C" void hal_stm32g474_timer_test_advance_us(uint64_t us);

static int s_fired;
static int s_managed_fired;
static hal_alarm_id_t s_last_id;

static int64_t alarm_once_cb(hal_alarm_id_t id, void *) {
  s_fired++;
  s_last_id = id;
  return 0;
}

static int64_t alarm_reschedule_cb(hal_alarm_id_t id, void *) {
  s_fired++;
  s_last_id = id;
  return (s_fired < 3) ? 50 : 0;
}

static void managed_cb(hal_timer_t, void *) { s_managed_fired++; }

static void advance_us(uint32_t us) {
  hal_delay_us(us);
  hal_stm32g474_timer_test_advance_us(us);
}

void setUp(void) {
  s_fired = 0;
  s_managed_fired = 0;
  s_last_id = HAL_ALARM_INVALID;
  hal_stm32g474_timer_test_reset();
}

void tearDown(void) {}

void test_stm32_alarm_fires_after_delay(void) {
  hal_alarm_id_t id =
      hal_timer_add_alarm_us(1000, alarm_once_cb, nullptr, false);
  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id);

  advance_us(999);
  TEST_ASSERT_EQUAL_INT(0, s_fired);

  advance_us(1);
  TEST_ASSERT_EQUAL_INT(1, s_fired);
  TEST_ASSERT_EQUAL_INT(id, s_last_id);
}

void test_stm32_alarm_reschedules_from_callback(void) {
  hal_alarm_id_t id =
      hal_timer_add_alarm_us(50, alarm_reschedule_cb, nullptr, false);
  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id);

  advance_us(50);
  TEST_ASSERT_EQUAL_INT(1, s_fired);
  TEST_ASSERT_EQUAL_INT(id, s_last_id);

  advance_us(49);
  TEST_ASSERT_EQUAL_INT(1, s_fired);

  advance_us(1);
  TEST_ASSERT_EQUAL_INT(2, s_fired);
  TEST_ASSERT_EQUAL_INT(id, s_last_id);

  advance_us(50);
  TEST_ASSERT_EQUAL_INT(3, s_fired);
  advance_us(50);
  TEST_ASSERT_EQUAL_INT(3, s_fired);
}

void test_stm32_cancel_prevents_fire(void) {
  hal_alarm_id_t id =
      hal_timer_add_alarm_us(100, alarm_once_cb, nullptr, false);
  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id);
  TEST_ASSERT_TRUE(hal_timer_cancel_alarm(id));

  advance_us(200);
  TEST_ASSERT_EQUAL_INT(0, s_fired);
}

void test_stm32_pool_capacity_and_destroy(void) {
  hal_timer_pool_t pool = hal_timer_pool_create_auto(1);
  TEST_ASSERT_NOT_NULL(pool);

  hal_timer_result_t res1 = HAL_TIMER_ERR_INTERNAL;
  hal_timer_result_t res2 = HAL_TIMER_ERR_INTERNAL;
  hal_alarm_id_t id1 = hal_timer_pool_add_alarm_us_ex(pool, 100, alarm_once_cb,
                                                      nullptr, false, &res1);
  hal_alarm_id_t id2 = hal_timer_pool_add_alarm_us_ex(pool, 100, alarm_once_cb,
                                                      nullptr, false, &res2);

  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id1);
  TEST_ASSERT_EQUAL_INT(HAL_ALARM_INVALID, id2);
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, (int)res1);
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_ERR_POOL_FULL, (int)res2);

  hal_timer_pool_destroy(pool);
  advance_us(200);
  TEST_ASSERT_EQUAL_INT(0, s_fired);
}

void test_stm32_long_delay_waits_for_full_deadline(void) {
  hal_alarm_id_t id =
      hal_timer_add_alarm_us(70000, alarm_once_cb, nullptr, false);
  TEST_ASSERT_NOT_EQUAL(HAL_ALARM_INVALID, id);

  advance_us(65535);
  TEST_ASSERT_EQUAL_INT(0, s_fired);
  advance_us(4464);
  TEST_ASSERT_EQUAL_INT(0, s_fired);
  advance_us(1);
  TEST_ASSERT_EQUAL_INT(1, s_fired);
}

void test_stm32_managed_timer_stop_prevents_fire(void) {
  hal_timer_t timer = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK,
                        hal_timer_create(HAL_TIMER_POOL_DEFAULT, 100, false,
                                         managed_cb, nullptr, &timer));
  TEST_ASSERT_NOT_NULL(timer);

  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_start(timer));
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_stop(timer));

  advance_us(200);
  TEST_ASSERT_EQUAL_INT(0, s_managed_fired);

  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_destroy(timer));
}

void test_stm32_managed_periodic_pause_resume(void) {
  hal_timer_t timer = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK,
                        hal_timer_create(HAL_TIMER_POOL_DEFAULT, 100, true,
                                         managed_cb, nullptr, &timer));
  TEST_ASSERT_NOT_NULL(timer);

  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_start(timer));
  advance_us(100);
  TEST_ASSERT_EQUAL_INT(1, s_managed_fired);

  int64_t remaining = -1;
  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK,
                        hal_timer_get_remaining_us(timer, &remaining));
  TEST_ASSERT_TRUE(remaining > 0);

  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_pause(timer));
  advance_us(500);
  TEST_ASSERT_EQUAL_INT(1, s_managed_fired);

  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_resume(timer));
  advance_us((uint32_t)remaining - 1u);
  TEST_ASSERT_EQUAL_INT(1, s_managed_fired);
  advance_us(1);
  TEST_ASSERT_EQUAL_INT(2, s_managed_fired);

  TEST_ASSERT_EQUAL_INT(HAL_TIMER_OK, hal_timer_destroy(timer));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stm32_alarm_fires_after_delay);
  RUN_TEST(test_stm32_alarm_reschedules_from_callback);
  RUN_TEST(test_stm32_cancel_prevents_fire);
  RUN_TEST(test_stm32_pool_capacity_and_destroy);
  RUN_TEST(test_stm32_long_delay_waits_for_full_deadline);
  RUN_TEST(test_stm32_managed_timer_stop_prevents_fire);
  RUN_TEST(test_stm32_managed_periodic_pause_resume);
  return UNITY_END();
}

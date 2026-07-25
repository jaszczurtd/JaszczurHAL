#include "hal/hal_app.h"
#include "utils/unity.h"

#include <stddef.h>

namespace {
enum call_t {
  CALL_INITIALIZATION,
  CALL_LOOPER,
  CALL_INITIALIZATION1,
  CALL_LOOPER1,
};

call_t s_calls[8];
size_t s_call_count;

void record_call(call_t call) {
  TEST_ASSERT_LESS_THAN_size_t(sizeof(s_calls) / sizeof(s_calls[0]),
                               s_call_count);
  s_calls[s_call_count++] = call;
}
} // namespace

extern "C" void initialization(void) { record_call(CALL_INITIALIZATION); }
extern "C" void looper(void) { record_call(CALL_LOOPER); }
extern "C" void initialization1(void) { record_call(CALL_INITIALIZATION1); }
extern "C" void looper1(void) { record_call(CALL_LOOPER1); }

void setUp(void) { s_call_count = 0u; }
void tearDown(void) {}

void test_secondary_initialization_runs_on_first_rp_task1_call(void) {
  app_start();
  TEST_ASSERT_EQUAL_size_t(1u, s_call_count);
  TEST_ASSERT_EQUAL_INT(CALL_INITIALIZATION, s_calls[0]);

  app_task0();
  TEST_ASSERT_EQUAL_size_t(2u, s_call_count);
  TEST_ASSERT_EQUAL_INT(CALL_LOOPER, s_calls[1]);

  app_task1();
  TEST_ASSERT_EQUAL_size_t(4u, s_call_count);
  TEST_ASSERT_EQUAL_INT(CALL_INITIALIZATION1, s_calls[2]);
  TEST_ASSERT_EQUAL_INT(CALL_LOOPER1, s_calls[3]);

  app_task1();
  TEST_ASSERT_EQUAL_size_t(5u, s_call_count);
  TEST_ASSERT_EQUAL_INT(CALL_LOOPER1, s_calls[4]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_secondary_initialization_runs_on_first_rp_task1_call);
  return UNITY_END();
}

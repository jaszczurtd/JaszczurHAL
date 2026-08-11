#include "hal/impl/.mock/hal_mock.h"
#include "hal/system/hal_sync.h"
#include "utils/unity.h"

#include <thread>

void setUp(void) { hal_mock_critical_section_reset(); }
void tearDown(void) {
  /* Every case must leave the section balanced and interrupts restored. */
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());
}

/* A single enter masks interrupts; the matching exit re-enables them. */
void test_single_enter_exit_toggles_irq(void) {
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());

  hal_critical_section_enter();
  TEST_ASSERT_FALSE(hal_mock_irq_enabled());
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_critical_depth());

  hal_critical_section_exit();
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());
}

/* The regression: nested sections must NOT re-enable interrupts on the inner
 * exit. Interrupts stay masked until the OUTERMOST exit. With the old
 * noInterrupts()/interrupts() pair the inner exit re-enabled early, leaving the
 * outer section running unprotected. */
void test_nested_reenables_only_on_outermost_exit(void) {
  hal_critical_section_enter(); /* depth 1 */
  hal_critical_section_enter(); /* depth 2 */
  hal_critical_section_enter(); /* depth 3 */
  TEST_ASSERT_EQUAL_UINT32(3, hal_mock_critical_depth());
  TEST_ASSERT_FALSE(hal_mock_irq_enabled());

  hal_critical_section_exit(); /* depth 2 - still masked */
  TEST_ASSERT_EQUAL_UINT32(2, hal_mock_critical_depth());
  TEST_ASSERT_FALSE(hal_mock_irq_enabled());

  hal_critical_section_exit(); /* depth 1 - still masked */
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_critical_depth());
  TEST_ASSERT_FALSE(hal_mock_irq_enabled());

  hal_critical_section_exit(); /* depth 0 - now re-enabled */
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());
}

/* An unbalanced exit (no matching enter) must not underflow the depth counter
 * or spuriously re-enable interrupts. */
void test_unbalanced_exit_is_safe(void) {
  hal_critical_section_exit(); /* nothing entered */
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());

  hal_critical_section_enter();
  hal_critical_section_exit();
  hal_critical_section_exit(); /* one too many */
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());
}

void test_mutex_try_lock_is_nonblocking_and_reusable(void) {
  hal_mutex_t mutex = hal_mutex_create();
  TEST_ASSERT_NOT_NULL(mutex);

  hal_mutex_lock(mutex);
  bool acquired_while_held = true;
  std::thread contender([&]() {
    acquired_while_held = hal_mutex_try_lock(mutex);
    if (acquired_while_held) {
      hal_mutex_unlock(mutex);
    }
  });
  contender.join();
  TEST_ASSERT_FALSE(acquired_while_held);
  hal_mutex_unlock(mutex);

  TEST_ASSERT_TRUE(hal_mutex_try_lock(mutex));
  hal_mutex_unlock(mutex);
  hal_mutex_destroy(mutex);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_single_enter_exit_toggles_irq);
  RUN_TEST(test_nested_reenables_only_on_outermost_exit);
  RUN_TEST(test_unbalanced_exit_is_safe);
  RUN_TEST(test_mutex_try_lock_is_nonblocking_and_reusable);
  return UNITY_END();
}

#include "hal/i2c/jh_i2c_recursive_lock.h"
#include "utils/unity.h"

#include <atomic>
#include <thread>

void setUp(void) {}
void tearDown(void) {}

static void destroy_lock(jh_i2c_recursive_lock_t *lock) {
  hal_mutex_destroy(lock->mutex);
  lock->mutex = nullptr;
}

void test_recursive_depth_keeps_underlying_mutex_locked(void) {
  jh_i2c_recursive_lock_t lock = {};
  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_acquire(&lock, 1u));
  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_acquire(&lock, 1u));
  TEST_ASSERT_FALSE(hal_mutex_try_lock(lock.mutex));

  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_release(&lock, 1u));
  TEST_ASSERT_FALSE(hal_mutex_try_lock(lock.mutex));
  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_release(&lock, 1u));
  TEST_ASSERT_TRUE(hal_mutex_try_lock(lock.mutex));
  hal_mutex_unlock(lock.mutex);
  destroy_lock(&lock);
}

void test_wrong_owner_cannot_release_lock(void) {
  jh_i2c_recursive_lock_t lock = {};
  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_acquire(&lock, 1u));
  TEST_ASSERT_FALSE(jh_i2c_recursive_lock_release(&lock, 2u));
  TEST_ASSERT_FALSE(hal_mutex_try_lock(lock.mutex));
  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_release(&lock, 1u));
  destroy_lock(&lock);
}

void test_nesting_depth_overflow_is_rejected(void) {
  jh_i2c_recursive_lock_t lock = {};
  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_acquire(&lock, 1u));
  HAL_ATOMIC_STORE(&lock.depth, UINT32_MAX, HAL_ATOMIC_RELEASE);
  TEST_ASSERT_FALSE(jh_i2c_recursive_lock_acquire(&lock, 1u));
  HAL_ATOMIC_STORE(&lock.depth, 1u, HAL_ATOMIC_RELEASE);
  TEST_ASSERT_TRUE(jh_i2c_recursive_lock_release(&lock, 1u));
  destroy_lock(&lock);
}

void test_distinct_owners_serialize_concurrent_access(void) {
  jh_i2c_recursive_lock_t lock = {};
  std::atomic<bool> failed(false);
  uint32_t counter = 0u;
  constexpr uint32_t kIterations = 10000u;

  auto worker = [&lock, &failed, &counter](uintptr_t owner) {
    for (uint32_t i = 0u; i < kIterations; ++i) {
      if (!jh_i2c_recursive_lock_acquire(&lock, owner) ||
          !jh_i2c_recursive_lock_acquire(&lock, owner)) {
        failed.store(true);
        return;
      }
      const uint32_t current = counter;
      std::this_thread::yield();
      counter = current + 1u;
      if (!jh_i2c_recursive_lock_release(&lock, owner) ||
          !jh_i2c_recursive_lock_release(&lock, owner)) {
        failed.store(true);
        return;
      }
    }
  };

  std::thread first(worker, 1u);
  std::thread second(worker, 2u);
  first.join();
  second.join();

  TEST_ASSERT_FALSE(failed.load());
  TEST_ASSERT_EQUAL_UINT32(2u * kIterations, counter);
  destroy_lock(&lock);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_recursive_depth_keeps_underlying_mutex_locked);
  RUN_TEST(test_wrong_owner_cannot_release_lock);
  RUN_TEST(test_nesting_depth_overflow_is_rejected);
  RUN_TEST(test_distinct_owners_serialize_concurrent_access);
  return UNITY_END();
}

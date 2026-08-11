#include "hal/impl/rp2040/rp2040_adc_shared.h"
#include "hal/system/hal_sync.h"
#include "utils/unity.h"

#include <atomic>
#include <mutex>
#include <thread>

struct hal_mutex_impl_t {
  std::mutex mutex;
};

static std::atomic<unsigned int> s_mutex_create_count{0u};
static std::atomic<unsigned int> s_adc_init_count{0u};
static std::atomic<unsigned int> s_unprotected_call_count{0u};
static std::atomic<unsigned int> s_selected_input{0u};
static thread_local hal_mutex_t s_locked_mutex = nullptr;

extern "C" hal_mutex_t hal_mutex_create(void) {
  s_mutex_create_count.fetch_add(1u);
  return new hal_mutex_impl_t;
}

extern "C" void hal_mutex_lock(hal_mutex_t mutex) {
  mutex->mutex.lock();
  s_locked_mutex = mutex;
}

extern "C" bool hal_mutex_try_lock(hal_mutex_t mutex) {
  if (!mutex->mutex.try_lock()) {
    return false;
  }
  s_locked_mutex = mutex;
  return true;
}

extern "C" void hal_mutex_unlock(hal_mutex_t mutex) {
  s_locked_mutex = nullptr;
  mutex->mutex.unlock();
}

extern "C" void hal_mutex_destroy(hal_mutex_t mutex) { delete mutex; }

extern "C" void hal_critical_section_enter(void) {}
extern "C" void hal_critical_section_exit(void) {}

static void record_adc_call(void) {
  if (s_locked_mutex == nullptr) {
    s_unprotected_call_count.fetch_add(1u);
  }
}

extern "C" void adc_init(void) {
  record_adc_call();
  s_adc_init_count.fetch_add(1u);
}

extern "C" void adc_gpio_init(unsigned int) { record_adc_call(); }

extern "C" void adc_select_input(unsigned int input) {
  record_adc_call();
  s_selected_input.store(input);
  std::this_thread::yield();
}

extern "C" uint16_t adc_read(void) {
  record_adc_call();
  std::this_thread::yield();
  return (uint16_t)(1000u + s_selected_input.load());
}

extern "C" void adc_set_temp_sensor_enabled(bool) { record_adc_call(); }

extern "C" void sleep_ms(uint32_t) {
  record_adc_call();
  std::this_thread::yield();
}

void setUp(void) {}
void tearDown(void) {}

static void
test_gpio_and_temperature_reads_share_one_adc_transaction_lock(void) {
  rp2040_adc_set_resolution(12u);
  TEST_ASSERT_EQUAL_INT(1000, rp2040_adc_read_gpio(26u));
  TEST_ASSERT_EQUAL_UINT16(1004u, rp2040_adc_read_temperature_raw());

  std::atomic<unsigned int> failures{0u};
  std::thread gpio_reader([&failures]() {
    for (unsigned int i = 0u; i < 500u; ++i) {
      if (rp2040_adc_read_gpio(26u) != 1000) {
        failures.fetch_add(1u);
      }
    }
  });
  std::thread temperature_reader([&failures]() {
    for (unsigned int i = 0u; i < 500u; ++i) {
      if (rp2040_adc_read_temperature_raw() != 1004u) {
        failures.fetch_add(1u);
      }
    }
  });
  gpio_reader.join();
  temperature_reader.join();

  TEST_ASSERT_EQUAL_UINT(0u, failures.load());
  TEST_ASSERT_EQUAL_UINT(0u, s_unprotected_call_count.load());
  TEST_ASSERT_EQUAL_UINT(1u, s_mutex_create_count.load());
  TEST_ASSERT_EQUAL_UINT(1u, s_adc_init_count.load());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_gpio_and_temperature_reads_share_one_adc_transaction_lock);
  return UNITY_END();
}

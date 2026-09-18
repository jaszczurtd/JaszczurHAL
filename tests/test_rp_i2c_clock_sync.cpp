// The RP I2C master clock accessors run on two host threads standing in for
// the two cores, with a real mutex behind the HAL bus lock.

#include "hal/i2c/hal_i2c.h"
#include "utils/unity.h"

#include <hardware/i2c.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

i2c_inst_t jh_test_i2c[2];
thread_local unsigned int jh_test_core_num = 0u;

extern "C" void hal_assert_fail(const char *msg) {
  std::fprintf(stderr, "HAL assert: %s\n", msg != nullptr ? msg : "");
  std::abort();
}

namespace {

constexpr uint8_t kBus = 0u;

void run_on_core1(const std::function<void()> &body, std::thread *out) {
  *out = std::thread([body] {
    jh_test_core_num = 1u;
    body();
  });
}

} // namespace

void setUp(void) {
  jh_test_core_num = 0u;
  TEST_ASSERT_EQUAL(HAL_OK, hal_i2c_init_bus(kBus, 4u, 5u, 100000u));
}

void tearDown(void) { hal_i2c_deinit_bus(kBus); }

static void test_clock_read_waits_for_the_bus_owner(void) {
  // Core 0 holds the bus and changes its clock as part of a longer sequence.
  hal_i2c_lock_bus(kBus);
  TEST_ASSERT_EQUAL(HAL_OK, hal_i2c_set_clock_bus(kBus, 400000u));

  std::atomic<bool> done{false};
  std::atomic<uint32_t> seen{0u};
  std::atomic<int> status{HAL_NONE};
  std::thread reader;
  run_on_core1(
      [&] {
        uint32_t clock = 0u;
        status.store(hal_i2c_get_clock_bus(kBus, &clock));
        seen.store(clock);
        done.store(true);
      },
      &reader);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const bool read_while_owned = done.load();
  hal_i2c_unlock_bus(kBus);
  reader.join();

  TEST_ASSERT_FALSE_MESSAGE(read_while_owned,
                            "clock read did not wait for the bus owner");
  TEST_ASSERT_EQUAL(HAL_OK, status.load());
  TEST_ASSERT_EQUAL_UINT32(400000u, seen.load());
}

static void test_clock_reads_and_changes_from_both_cores(void) {
  constexpr unsigned kRounds = 20000u;
  std::atomic<unsigned> unexpected{0u};
  std::thread reader;
  run_on_core1(
      [&] {
        for (unsigned round = 0u; round < kRounds; ++round) {
          uint32_t clock = 0u;
          if (hal_i2c_get_clock_bus(kBus, &clock) != HAL_OK ||
              (clock != 100000u && clock != 400000u)) {
            unexpected.fetch_add(1u);
          }
        }
      },
      &reader);
  for (unsigned round = 0u; round < kRounds; ++round) {
    (void)hal_i2c_set_clock_bus(kBus, (round & 1u) != 0u ? 400000u : 100000u);
  }
  reader.join();
  TEST_ASSERT_EQUAL_UINT32(0u, unexpected.load());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_clock_read_waits_for_the_bus_owner);
  RUN_TEST(test_clock_reads_and_changes_from_both_cores);
  return UNITY_END();
}

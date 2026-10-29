#include "hal/spi/jh_spi_ticket.h"
#include "utils/unity.h"

#include <atomic>
#include <thread>

void setUp(void) {}
void tearDown(void) {}

static void awaitTurn(jh_spi_ticket_t *queue, uint32_t ticket) {
  while (!jh_spi_ticket_ready(queue, ticket)) {
    std::this_thread::yield();
  }
}

void test_a_new_band_cannot_overtake_an_already_waiting_reader(void) {
  jh_spi_ticket_t queue = {};
  const uint32_t display = jh_spi_ticket_reserve(&queue);
  std::atomic<bool> reserved{false};
  std::atomic<bool> readerRan{false};
  std::thread reader([&]() {
    const uint32_t ticket = jh_spi_ticket_reserve(&queue);
    reserved.store(true, std::memory_order_release);
    awaitTurn(&queue, ticket);
    readerRan.store(true, std::memory_order_relaxed);
    jh_spi_ticket_release(&queue);
  });
  while (!reserved.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  const uint32_t nextBand = jh_spi_ticket_reserve(&queue);
  const bool nextWasBlocked = !jh_spi_ticket_ready(&queue, nextBand);
  const bool displayOwned = jh_spi_ticket_ready(&queue, display);
  jh_spi_ticket_release(&queue);
  awaitTurn(&queue, nextBand);
  const bool readerWentFirst = readerRan.load(std::memory_order_relaxed);
  jh_spi_ticket_release(&queue);
  reader.join();
  TEST_ASSERT_TRUE(nextWasBlocked);
  TEST_ASSERT_TRUE(displayOwned);
  TEST_ASSERT_TRUE(readerWentFirst);
}

void test_turns_wrap_and_publish_between_competing_callers(void) {
  jh_spi_ticket_t queue = {UINT32_MAX - 3u, UINT32_MAX - 3u};
  uint32_t expected = UINT32_MAX - 3u;
  uint32_t writes = 0u;
  bool ordered = true;
  auto worker = [&]() {
    for (uint32_t i = 0u; i < 2000u; ++i) {
      const uint32_t ticket = jh_spi_ticket_reserve(&queue);
      awaitTurn(&queue, ticket);
      ordered = ordered && (ticket == expected);
      expected++;
      writes++;
      jh_spi_ticket_release(&queue);
    }
  };
  std::thread first(worker);
  std::thread second(worker);
  std::thread third(worker);
  first.join();
  second.join();
  third.join();
  TEST_ASSERT_TRUE(ordered);
  TEST_ASSERT_EQUAL_UINT32(6000u, writes);
  TEST_ASSERT_EQUAL_UINT32(expected, queue.next);
  TEST_ASSERT_EQUAL_UINT32(expected, queue.serving);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_new_band_cannot_overtake_an_already_waiting_reader);
  RUN_TEST(test_turns_wrap_and_publish_between_competing_callers);
  return UNITY_END();
}

// jh_spsc_ring: order, full and empty edges, counter wrap, and one producer
// thread against one consumer thread.

#include "hal/core/jh_spsc_ring.h"
#include "utils/unity.h"

#include <stdint.h>
#include <thread>

namespace {

struct Item {
  uint32_t seq;
  uint32_t check;
};

Item s_storage[5];
jh_spsc_ring_t s_ring;

Item item(uint32_t seq) { return Item{seq, seq ^ 0xA5A5A5A5u}; }

} // namespace

void setUp(void) { jh_spsc_ring_init(&s_ring, s_storage, sizeof(Item), 5u); }
void tearDown(void) {}

void test_elements_leave_in_the_order_they_came(void) {
  for (uint32_t i = 0; i < 5u; ++i) {
    const Item in = item(i);
    TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_ring, &in));
  }
  TEST_ASSERT_EQUAL_UINT32(5u, jh_spsc_ring_count(&s_ring));
  for (uint32_t i = 0; i < 5u; ++i) {
    Item out = {};
    TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_ring, &out));
    TEST_ASSERT_EQUAL_UINT32(i, out.seq);
  }
  Item out = {};
  TEST_ASSERT_FALSE(jh_spsc_ring_pop(&s_ring, &out));
}

void test_a_full_ring_refuses_and_keeps_its_content(void) {
  for (uint32_t i = 0; i < 5u; ++i) {
    const Item in = item(i);
    TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_ring, &in));
  }
  const Item extra = item(99u);
  TEST_ASSERT_FALSE(jh_spsc_ring_push(&s_ring, &extra));
  Item out = {};
  TEST_ASSERT_TRUE(jh_spsc_ring_pop(&s_ring, &out));
  TEST_ASSERT_EQUAL_UINT32(0u, out.seq);
  TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_ring, &extra));
}

void test_positions_cycle_without_losing_order_or_fill_level(void) {
  /* Partial fills walk the positions across their wrap many times. */
  uint32_t next_in = 0u;
  uint32_t next_out = 0u;
  for (uint32_t round = 0; round < 37u; ++round) {
    const uint32_t burst = 1u + (round % 5u);
    for (uint32_t i = 0; i < burst; ++i) {
      const Item in = item(next_in++);
      TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_ring, &in));
    }
    TEST_ASSERT_EQUAL_UINT32(burst, jh_spsc_ring_count(&s_ring));
    const Item extra = item(next_in);
    const bool room = burst < 5u;
    TEST_ASSERT_EQUAL(room, jh_spsc_ring_push(&s_ring, &extra));
    if (room) {
      ++next_in;
    }
    Item out = {};
    while (jh_spsc_ring_pop(&s_ring, &out)) {
      TEST_ASSERT_EQUAL_UINT32(next_out, out.seq);
      ++next_out;
    }
    TEST_ASSERT_EQUAL_UINT32(next_in, next_out);
  }
}

void test_peek_keeps_the_element_until_dropped(void) {
  const Item in = item(7u);
  TEST_ASSERT_NULL(jh_spsc_ring_peek(&s_ring));
  TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_ring, &in));
  const Item *front = static_cast<const Item *>(jh_spsc_ring_peek(&s_ring));
  TEST_ASSERT_NOT_NULL(front);
  TEST_ASSERT_EQUAL_UINT32(7u, front->seq);
  TEST_ASSERT_EQUAL_UINT32(1u, jh_spsc_ring_count(&s_ring));
  jh_spsc_ring_drop(&s_ring);
  TEST_ASSERT_EQUAL_UINT32(0u, jh_spsc_ring_count(&s_ring));
}

void test_clear_empties_and_a_ring_without_storage_holds_nothing(void) {
  const Item in = item(1u);
  TEST_ASSERT_TRUE(jh_spsc_ring_push(&s_ring, &in));
  jh_spsc_ring_clear(&s_ring);
  TEST_ASSERT_EQUAL_UINT32(0u, jh_spsc_ring_count(&s_ring));
  jh_spsc_ring_t none;
  jh_spsc_ring_init(&none, nullptr, sizeof(Item), 4u);
  TEST_ASSERT_FALSE(jh_spsc_ring_push(&none, &in));
  Item out = {};
  TEST_ASSERT_FALSE(jh_spsc_ring_pop(&none, &out));
}

void test_a_producer_thread_and_a_consumer_thread_lose_nothing(void) {
  constexpr uint32_t kItems = 200000u;
  std::thread producer([] {
    for (uint32_t i = 0; i < kItems;) {
      const Item in = item(i);
      if (jh_spsc_ring_push(&s_ring, &in)) {
        ++i;
      } else {
        std::this_thread::yield();
      }
    }
  });
  uint32_t expected = 0u;
  uint32_t corrupt = 0u;
  while (expected < kItems) {
    Item out = {};
    if (jh_spsc_ring_pop(&s_ring, &out)) {
      if (out.seq != expected || out.check != (out.seq ^ 0xA5A5A5A5u)) {
        ++corrupt;
      }
      ++expected;
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();
  TEST_ASSERT_EQUAL_UINT32(0u, corrupt);
  TEST_ASSERT_EQUAL_UINT32(0u, jh_spsc_ring_count(&s_ring));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_elements_leave_in_the_order_they_came);
  RUN_TEST(test_a_full_ring_refuses_and_keeps_its_content);
  RUN_TEST(test_positions_cycle_without_losing_order_or_fill_level);
  RUN_TEST(test_peek_keeps_the_element_until_dropped);
  RUN_TEST(test_clear_empties_and_a_ring_without_storage_holds_nothing);
  RUN_TEST(test_a_producer_thread_and_a_consumer_thread_lose_nothing);
  return UNITY_END();
}

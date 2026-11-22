#include "hal/core/hal_array.h"
#include "hal/debug/hal_debug_events.h"
#include "utils/unity.h"

typedef struct {
  uint32_t timestamp;
  int32_t value;
} record_t;

static hal_debug_events_t s_queue;
static record_t s_storage[3];
static record_t s_received[3];
static unsigned s_calls;
static hal_status_t s_consumerStatus;

void setUp(void) {
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_debug_events_init(&s_queue, s_storage, sizeof(record_t),
                                          COUNTOF(s_storage)));
  s_calls = 0U;
  s_consumerStatus = HAL_OK;
}

void tearDown(void) {}

static hal_status_t consume(const void *record, void *context) {
  TEST_ASSERT_EQUAL_PTR(&s_queue, context);
  TEST_ASSERT_LESS_THAN_UINT(COUNTOF(s_received), s_calls);
  s_received[s_calls++] = *(const record_t *)record;
  return s_consumerStatus;
}

static void test_publish_captures_values_and_never_calls_the_consumer(void) {
  record_t record = {0xFFFFFFF0U, -123};
  TEST_ASSERT_EQUAL(HAL_OK, hal_debug_events_publish(&s_queue, &record));
  record.timestamp = 10U;
  record.value = 456;
  TEST_ASSERT_EQUAL(HAL_OK, hal_debug_events_publish(&s_queue, &record));
  TEST_ASSERT_EQUAL_UINT(0U, s_calls);
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_debug_events_poll(&s_queue, 0U, consume, &s_queue));
  TEST_ASSERT_EQUAL_UINT(0U, s_calls);
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_debug_events_poll(&s_queue, 1U, consume, &s_queue));
  TEST_ASSERT_EQUAL_UINT(1U, s_calls);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF0U, s_received[0].timestamp);
  TEST_ASSERT_EQUAL_INT32(-123, s_received[0].value);
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_debug_events_poll(&s_queue, 10U, consume, &s_queue));
  TEST_ASSERT_EQUAL_UINT(2U, s_calls);
  TEST_ASSERT_EQUAL_UINT32(10U, s_received[1].timestamp);
  TEST_ASSERT_EQUAL_INT32(456, s_received[1].value);
}

static void
test_stalled_consumer_preserves_pending_records_and_counts_overflow(void) {
  for (int32_t i = 0; i < 3; ++i) {
    const record_t record = {100U, i};
    TEST_ASSERT_EQUAL(HAL_OK, hal_debug_events_publish(&s_queue, &record));
  }
  s_consumerStatus = HAL_EAGAIN;
  TEST_ASSERT_EQUAL(HAL_EAGAIN,
                    hal_debug_events_poll(&s_queue, 3U, consume, &s_queue));
  TEST_ASSERT_EQUAL_UINT(1U, s_calls);
  const record_t later = {200U, 99};
  for (unsigned i = 0U; i < 100U; ++i) {
    TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_debug_events_publish(&s_queue, &later));
  }
  TEST_ASSERT_EQUAL_UINT32(100U, hal_debug_events_dropped(&s_queue));
  s_calls = 0U;
  s_consumerStatus = HAL_OK;
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_debug_events_poll(&s_queue, 3U, consume, &s_queue));
  for (int32_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL_INT32(i, s_received[i].value);
  }
  TEST_ASSERT_EQUAL_UINT32(100U, hal_debug_events_dropped(&s_queue));
  TEST_ASSERT_EQUAL(HAL_OK, hal_debug_events_publish(&s_queue, &later));
}

static void test_invalid_arguments_do_not_destroy_pending_records(void) {
  const record_t record = {123U, 456};
  TEST_ASSERT_EQUAL(HAL_OK, hal_debug_events_publish(&s_queue, &record));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_debug_events_init(NULL, s_storage, 8U, 3U));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_debug_events_init(&s_queue, NULL, 8U, 3U));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_debug_events_init(&s_queue, s_storage, 0U, 3U));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_debug_events_init(&s_queue, s_storage, 8U, 0U));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_debug_events_publish(NULL, &record));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_debug_events_publish(&s_queue, NULL));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_debug_events_poll(NULL, 1U, consume, &s_queue));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    hal_debug_events_poll(&s_queue, 1U, NULL, NULL));
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_debug_events_poll(&s_queue, 1U, consume, &s_queue));
  TEST_ASSERT_EQUAL_INT32(456, s_received[0].value);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_publish_captures_values_and_never_calls_the_consumer);
  RUN_TEST(test_stalled_consumer_preserves_pending_records_and_counts_overflow);
  RUN_TEST(test_invalid_arguments_do_not_destroy_pending_records);
  return UNITY_END();
}

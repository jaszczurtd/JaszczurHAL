#include "hal/impl/esp32/jh_esp32_serial_tx.h"
#include "utils/unity.h"
#include <string.h>

static size_t s_room;
static unsigned s_calls;
static char s_received[2048];
static size_t s_received_length;
static int s_error;

int usb_serial_jtag_write_bytes(const void *data, size_t length,
                                uint32_t ticks_to_wait) {
  TEST_ASSERT_EQUAL_UINT32(0U, ticks_to_wait);
  ++s_calls;
  if (s_error != 0) {
    return s_error;
  }
  if (length > s_room) {
    return 0;
  }
  TEST_ASSERT_TRUE(s_received_length + length <= sizeof(s_received));
  memcpy(s_received + s_received_length, data, length);
  s_received_length += length;
  s_room -= length;
  return (int)length;
}

void setUp(void) {
  s_room = 0U;
  s_calls = 0U;
  s_received_length = 0U;
  s_error = 0;
}
void tearDown(void) {}

static void test_long_record_drains_through_smaller_atomic_driver_buffer(void) {
  static char message[1536];
  for (size_t i = 0U; i < sizeof(message); ++i) {
    message[i] = (char)('A' + i % 26U);
  }
  size_t offset = 0U;
  for (unsigned attempt = 0U; attempt < 1536U && offset < sizeof(message);
       ++attempt) {
    /* Includes an application-installed buffer smaller than one USB packet. */
    s_room = attempt % 2U == 0U ? 7U : 512U;
    s_calls = 0U;
    const int written =
        jh_esp32_usb_try_write(message + offset, sizeof(message) - offset);
    TEST_ASSERT_GREATER_THAN_INT(0, written);
    TEST_ASSERT_TRUE(s_calls <= 10U);
    offset += (size_t)written;
  }
  TEST_ASSERT_EQUAL_UINT32(sizeof(message), offset);
  TEST_ASSERT_EQUAL_MEMORY(message, s_received, sizeof(message));
}

static void test_full_buffer_and_driver_error_return_without_waiting(void) {
  static const char message[1024] = {0};
  TEST_ASSERT_EQUAL_INT(0, jh_esp32_usb_try_write(message, sizeof(message)));
  TEST_ASSERT_EQUAL_UINT32(10U, s_calls);
  TEST_ASSERT_EQUAL_UINT32(0U, s_received_length);
  s_calls = 0U;
  s_error = -1;
  TEST_ASSERT_EQUAL_INT(-1, jh_esp32_usb_try_write(message, sizeof(message)));
  TEST_ASSERT_EQUAL_UINT32(1U, s_calls);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_long_record_drains_through_smaller_atomic_driver_buffer);
  RUN_TEST(test_full_buffer_and_driver_error_return_without_waiting);
  return UNITY_END();
}

#include "hal/impl/.mock/hal_mock.h"
#include "hal/usb/hal_usb.h"
#include "utils/unity.h"

#include <string.h>

namespace {

bool s_reset_hook_called;

void reset_hook(void *user) {
  bool *called = static_cast<bool *>(user);
  *called = true;
}

} // namespace

void setUp(void) {
  hal_mock_usb_reset();
  s_reset_hook_called = false;
}

void tearDown(void) {}

void test_usb_rejects_io_before_initialization(void) {
  bool connected = true;
  size_t count = 123u;
  uint8_t byte = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_usb_cdc_is_connected(&connected));
  TEST_ASSERT_FALSE(connected);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_usb_cdc_available(&count));
  TEST_ASSERT_EQUAL_size_t(0u, count);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_usb_cdc_read(&byte, 1u, &count));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_usb_cdc_write(&byte, 1u, 10u, &count));
}

void test_usb_init_connection_and_deinit_are_explicit(void) {
  bool connected = true;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_init());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_init());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_cdc_is_connected(&connected));
  TEST_ASSERT_FALSE(connected);

  hal_mock_usb_set_connected(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_cdc_is_connected(&connected));
  TEST_ASSERT_TRUE(connected);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_task());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_deinit());
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_usb_task());
}

void test_usb_cdc_read_write_and_disconnect_statuses(void) {
  const uint8_t rx[] = {0x10u, 0x20u, 0x30u};
  const uint8_t tx[] = {'o', 'k'};
  uint8_t read_buffer[4] = {};
  size_t count = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_init());
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_usb_cdc_write(tx, sizeof(tx), 10u, &count));
  TEST_ASSERT_EQUAL_size_t(0u, count);

  hal_mock_usb_set_connected(true);
  hal_mock_usb_inject_rx(rx, sizeof(rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_cdc_available(&count));
  TEST_ASSERT_EQUAL_size_t(sizeof(rx), count);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_usb_cdc_read(read_buffer, sizeof(read_buffer), &count));
  TEST_ASSERT_EQUAL_size_t(sizeof(rx), count);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, read_buffer, sizeof(rx));
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_usb_cdc_read(read_buffer, sizeof(read_buffer), &count));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_cdc_write(tx, sizeof(tx), 10u, &count));
  TEST_ASSERT_EQUAL_size_t(sizeof(tx), count);
  TEST_ASSERT_EQUAL_size_t(sizeof(tx), hal_mock_usb_tx_size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, hal_mock_usb_tx_data(), sizeof(tx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_cdc_flush(10u));
}

void test_usb_cdc_write_reports_bounded_mock_backpressure(void) {
  uint8_t payload[4097];
  memset(payload, 0xA5, sizeof(payload));
  size_t written = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_init());
  hal_mock_usb_set_connected(true);
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      hal_usb_cdc_write(payload, sizeof(payload), 10u, &written));
  TEST_ASSERT_EQUAL_size_t(4096u, written);
}

void test_usb_bootloader_reset_notifies_hook(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_init());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_set_bootloader_reset_hook(
                                    reset_hook, &s_reset_hook_called));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_reset_to_bootloader());
  TEST_ASSERT_TRUE(s_reset_hook_called);
  TEST_ASSERT_TRUE(hal_mock_usb_reset_requested());
}

void test_usb_validates_output_arguments(void) {
  uint8_t value = 0u;
  size_t count = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_usb_cdc_is_connected(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_usb_cdc_available(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_usb_cdc_read(nullptr, 1u, &count));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_usb_cdc_read(&value, 1u, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_usb_cdc_write(nullptr, 1u, 0u, &count));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_usb_cdc_write(&value, 1u, 0u, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_usb_rejects_io_before_initialization);
  RUN_TEST(test_usb_init_connection_and_deinit_are_explicit);
  RUN_TEST(test_usb_cdc_read_write_and_disconnect_statuses);
  RUN_TEST(test_usb_cdc_write_reports_bounded_mock_backpressure);
  RUN_TEST(test_usb_bootloader_reset_notifies_hook);
  RUN_TEST(test_usb_validates_output_arguments);
  return UNITY_END();
}

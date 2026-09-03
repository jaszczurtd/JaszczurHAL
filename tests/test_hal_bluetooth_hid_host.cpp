#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/bluetooth/hal_bluetooth_hid_host.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

namespace {

hal_bluetooth_classic_t s_classic = nullptr;
hal_bluetooth_hid_host_t s_hid = nullptr;

const hal_bluetooth_classic_address_t kMouseAddress = {
    {0x01u, 0x23u, 0x45u, 0x67u, 0x89u, 0xabu}};

const uint8_t kMouseDescriptor[] = {
    0x05u, 0x01u, 0x09u, 0x02u, 0xa1u, 0x01u, 0x09u, 0x01u, 0xa1u, 0x00u,
    0x05u, 0x09u, 0x19u, 0x01u, 0x29u, 0x03u, 0x15u, 0x00u, 0x25u, 0x01u,
    0x95u, 0x03u, 0x75u, 0x01u, 0x81u, 0x02u, 0x95u, 0x01u, 0x75u, 0x05u,
    0x81u, 0x03u, 0x05u, 0x01u, 0x09u, 0x30u, 0x09u, 0x31u, 0x15u, 0x81u,
    0x25u, 0x7fu, 0x75u, 0x08u, 0x95u, 0x02u, 0x81u, 0x06u, 0xc0u, 0xc0u,
};

void open_ready_connected(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_open(&s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_classic_inject_ready());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_hid_host_open(s_classic, &s_hid));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_hid_host_connect(s_hid, &kMouseAddress));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_hid_inject_connected(&kMouseAddress));
}

} // namespace

void setUp(void) {
  s_hid = nullptr;
  s_classic = nullptr;
  hal_mock_bluetooth_classic_reset();
  hal_mock_bluetooth_hid_runtime_full_reset();
  hal_mock_bluetooth_classic_runtime_full_reset();
}

void tearDown(void) {
  if (s_hid != nullptr) {
    (void)hal_bluetooth_hid_host_close(s_hid);
  }
  if (s_classic != nullptr) {
    (void)hal_bluetooth_classic_close(s_classic);
  }
  s_hid = nullptr;
  s_classic = nullptr;
  hal_mock_bluetooth_classic_reset();
  hal_mock_bluetooth_hid_runtime_full_reset();
  hal_mock_bluetooth_classic_runtime_full_reset();
}

void test_mouse_descriptor_and_raw_input_report_are_copied(void) {
  open_ready_connected();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_hid_inject_descriptor(
                            kMouseDescriptor, sizeof(kMouseDescriptor)));
  uint8_t descriptor[HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN]{};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_hid_host_descriptor(s_hid, descriptor,
                                                sizeof(descriptor), &length));
  TEST_ASSERT_EQUAL_UINT(sizeof(kMouseDescriptor), length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMouseDescriptor, descriptor, length);

  hal_bluetooth_hid_report_t injected{};
  injected.type = HAL_BLUETOOTH_HID_REPORT_INPUT;
  injected.length = 3u;
  injected.data[0] = 0x01u;
  injected.data[1] = 0x08u;
  injected.data[2] = 0xfcu;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_hid_inject_report(&injected));
  memset(injected.data, 0, sizeof(injected.data));

  hal_bluetooth_hid_report_t received{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_hid_host_report_next(s_hid, &received));
  TEST_ASSERT_EQUAL_INT(HAL_BLUETOOTH_HID_REPORT_INPUT, received.type);
  TEST_ASSERT_EQUAL_UINT8(3u, received.length);
  TEST_ASSERT_EQUAL_HEX8(0x01u, received.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x08u, received.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0xfcu, received.data[2]);
}

void test_output_feature_and_input_requests_are_generic(void) {
  open_ready_connected();
  hal_bluetooth_hid_report_t output{};
  output.type = HAL_BLUETOOTH_HID_REPORT_OUTPUT;
  output.report_id = 2u;
  output.length = 2u;
  output.data[0] = 0xa5u;
  output.data[1] = 0x5au;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_hid_host_report_send(s_hid, &output));
  output.type = HAL_BLUETOOTH_HID_REPORT_FEATURE;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_hid_host_report_send(s_hid, &output));
  output.type = HAL_BLUETOOTH_HID_REPORT_INPUT;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_bluetooth_hid_host_report_send(s_hid, &output));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_hid_host_report_request(
                                    s_hid, HAL_BLUETOOTH_HID_REPORT_INPUT, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_hid_host_report_request(
                            s_hid, HAL_BLUETOOTH_HID_REPORT_FEATURE, 2u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_bluetooth_hid_host_report_request(
                            s_hid, HAL_BLUETOOTH_HID_REPORT_OUTPUT, 2u));
}

void test_report_queue_overflow_is_explicit(void) {
  open_ready_connected();
  for (size_t index = 0u; index < HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH + 1u;
       ++index) {
    hal_bluetooth_hid_report_t report{};
    report.type = HAL_BLUETOOTH_HID_REPORT_INPUT;
    report.length = 1u;
    report.data[0] = (uint8_t)index;
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_bluetooth_hid_inject_report(&report));
  }
  hal_bluetooth_hid_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_hid_host_get_info(s_hid, &info));
  TEST_ASSERT_EQUAL_UINT32(1u, info.dropped_reports);
  TEST_ASSERT_EQUAL_UINT(HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH,
                         info.pending_reports);
  hal_bluetooth_hid_report_t report{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_bluetooth_hid_host_report_next(s_hid, &report));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_hid_host_report_next(s_hid, &report));
  TEST_ASSERT_EQUAL_UINT8(0u, report.data[0]);
}

void test_classic_cannot_close_while_hid_profile_is_attached(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_open(&s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_hid_host_open(s_classic, &s_hid));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_bluetooth_classic_close(s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_hid_host_close(s_hid));
  s_hid = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_close(s_classic));
  s_classic = nullptr;
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mouse_descriptor_and_raw_input_report_are_copied);
  RUN_TEST(test_output_feature_and_input_requests_are_generic);
  RUN_TEST(test_report_queue_overflow_is_explicit);
  RUN_TEST(test_classic_cannot_close_while_hid_profile_is_attached);
  return UNITY_END();
}

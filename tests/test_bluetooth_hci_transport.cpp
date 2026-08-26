#include "hal/bluetooth/jh_bluetooth_hci_transport.h"
#include "utils/unity.h"

#include <cstring>

namespace {

constexpr size_t kFrameCount = 16u;
constexpr size_t kFrameCapacity = 32u;

struct fake_controller_t {
  hal_status_t init_status;
  hal_status_t read_status;
  hal_status_t write_status;
  uint8_t factory_address[6];
  uint8_t frames[kFrameCount][kFrameCapacity];
  uint32_t lengths[kFrameCount];
  size_t frame_count;
  size_t frame_index;
  uint8_t write[kFrameCapacity];
  size_t write_length;
  unsigned init_calls;
  unsigned read_calls;
  unsigned write_calls;
};

struct packet_sink_t {
  jh_bluetooth_hci_transport_t *transport;
  unsigned packets;
  uint8_t last_type;
  uint16_t last_size;
  hal_status_t reentry_status;
  bool try_reentry;
};

fake_controller_t s_fake;
packet_sink_t s_sink;
jh_bluetooth_controller_t s_controller;
jh_bluetooth_hci_transport_t s_transport;
alignas(4) uint8_t s_receive[kFrameCapacity];

hal_status_t hci_init(void *context) {
  auto *fake = static_cast<fake_controller_t *>(context);
  ++fake->init_calls;
  return fake->init_status;
}

hal_status_t hci_read(void *context, uint8_t *buffer, uint32_t capacity,
                      uint32_t *out_length) {
  auto *fake = static_cast<fake_controller_t *>(context);
  ++fake->read_calls;
  if (fake->read_status != HAL_OK) {
    return fake->read_status;
  }
  if (fake->frame_index == fake->frame_count) {
    *out_length = 0u;
    return HAL_OK;
  }
  const size_t index = fake->frame_index++;
  *out_length = fake->lengths[index];
  if (*out_length <= capacity) {
    std::memcpy(buffer, fake->frames[index], *out_length);
  }
  return HAL_OK;
}

hal_status_t hci_write(void *context, uint8_t *buffer, size_t length) {
  auto *fake = static_cast<fake_controller_t *>(context);
  ++fake->write_calls;
  if (fake->write_status != HAL_OK) {
    return fake->write_status;
  }
  TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(fake->write), length);
  std::memcpy(fake->write, buffer, length);
  fake->write_length = length;
  return HAL_OK;
}

hal_status_t read_factory_address(void *context, uint8_t address[6]) {
  auto *fake = static_cast<fake_controller_t *>(context);
  std::memcpy(address, fake->factory_address, sizeof(fake->factory_address));
  return HAL_OK;
}

void receive_packet(void *context, uint8_t packet_type, uint8_t *,
                    uint16_t size) {
  auto *sink = static_cast<packet_sink_t *>(context);
  ++sink->packets;
  sink->last_type = packet_type;
  sink->last_size = size;
  if (sink->try_reentry) {
    sink->reentry_status = jh_bluetooth_hci_transport_service(sink->transport);
  }
}

void queue_frame(uint8_t packet_type, const uint8_t *payload,
                 uint32_t payload_size) {
  TEST_ASSERT_LESS_THAN_size_t(kFrameCount, s_fake.frame_count);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(
      kFrameCapacity - JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE, payload_size);
  const size_t index = s_fake.frame_count++;
  s_fake.frames[index][3] = packet_type;
  if (payload_size != 0u) {
    std::memcpy(&s_fake.frames[index][JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE],
                payload, payload_size);
  }
  s_fake.lengths[index] = JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE + payload_size;
}

jh_bluetooth_hci_transport_snapshot_t snapshot(void) {
  jh_bluetooth_hci_transport_snapshot_t result{};
  jh_bluetooth_hci_transport_snapshot(&s_transport, &result);
  return result;
}

} // namespace

void setUp(void) {
  std::memset(&s_fake, 0, sizeof(s_fake));
  std::memset(&s_sink, 0, sizeof(s_sink));
  std::memset(&s_transport, 0, sizeof(s_transport));
  s_fake.init_status = HAL_OK;
  s_fake.read_status = HAL_OK;
  s_fake.write_status = HAL_OK;
  const uint8_t address[6] = {0x28u, 0xcdu, 0xc1u, 0x19u, 0x18u, 0x18u};
  std::memcpy(s_fake.factory_address, address, sizeof(address));
  s_controller = {
      &s_fake,  nullptr,  nullptr,   nullptr,
      hci_init, hci_read, hci_write, read_factory_address,
  };
  s_sink.transport = &s_transport;
  s_sink.reentry_status = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_hci_transport_init(&s_transport, &s_controller,
                                              s_receive, sizeof(s_receive),
                                              receive_packet, &s_sink));
}

void tearDown(void) {}

void test_open_uses_factory_wifi_address_plus_one(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_hci_transport_open(&s_transport));
  uint8_t address[6]{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_hci_transport_address(&s_transport, address));
  const uint8_t expected[6] = {0x28u, 0xcdu, 0xc1u, 0x19u, 0x18u, 0x19u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, address, sizeof(expected));
  TEST_ASSERT_EQUAL_UINT(1u, s_fake.init_calls);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        jh_bluetooth_hci_transport_open(&s_transport));
}

void test_open_propagates_controller_initialization_failure(void) {
  s_fake.init_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_bluetooth_hci_transport_open(&s_transport));
  uint8_t address[6]{};
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNINIT, jh_bluetooth_hci_transport_address(&s_transport, address));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, snapshot().last_status);
}

void test_receive_drain_is_bounded_and_resumes_next_service(void) {
  const uint8_t payload = 0x5au;
  for (unsigned index = 0u; index < 10u; ++index) {
    queue_frame(0x04u, &payload, sizeof(payload));
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_hci_transport_open(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_hci_transport_service(&s_transport));
  TEST_ASSERT_EQUAL_UINT(JH_BLUETOOTH_HCI_SERVICE_BUDGET, s_sink.packets);
  TEST_ASSERT_EQUAL_UINT(JH_BLUETOOTH_HCI_SERVICE_BUDGET, s_fake.read_calls);
  TEST_ASSERT_EQUAL_UINT(1u, snapshot().drain_budget_hits);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_hci_transport_service(&s_transport));
  TEST_ASSERT_EQUAL_UINT(10u, s_sink.packets);
  TEST_ASSERT_EQUAL_UINT(11u, s_fake.read_calls);
  TEST_ASSERT_EQUAL_UINT(1u, snapshot().drain_budget_hits);
  TEST_ASSERT_EQUAL_UINT(2u, snapshot().service_calls);
}

void test_flow_control_command_completions_are_recorded(void) {
  const uint8_t host_buffer[] = {0x0eu, 0x04u, 0x01u, 0x33u, 0x0cu, 0x00u};
  const uint8_t flow_control[] = {0x0eu, 0x04u, 0x01u, 0x31u, 0x0cu, 0x0cu};
  queue_frame(0x04u, host_buffer, sizeof(host_buffer));
  queue_frame(0x04u, flow_control, sizeof(flow_control));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_hci_transport_open(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_hci_transport_service(&s_transport));
  const auto value = snapshot();
  TEST_ASSERT_EQUAL_UINT(2u, value.rx_packets);
  TEST_ASSERT_EQUAL_UINT(2u, value.rx_event_packets);
  TEST_ASSERT_EQUAL_HEX8(0x00u, value.host_buffer_size_status);
  TEST_ASSERT_EQUAL_HEX8(0x0cu, value.controller_to_host_flow_control_status);
}

void test_malformed_and_failed_reads_propagate_hal_status(void) {
  s_fake.frame_count = 1u;
  s_fake.lengths[0] = JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE - 1u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_hci_transport_open(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_bluetooth_hci_transport_service(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, snapshot().last_status);

  s_fake.frame_index = 0u;
  s_fake.lengths[0] = sizeof(s_receive) + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_bluetooth_hci_transport_service(&s_transport));

  s_fake.frame_index = s_fake.frame_count;
  s_fake.read_status = HAL_ETIMEOUT;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        jh_bluetooth_hci_transport_service(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, snapshot().last_status);
}

void test_send_prepends_type_and_counts_only_successful_packets(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_hci_transport_open(&s_transport));
  alignas(4) uint8_t frame[12]{};
  uint8_t *const packet = &frame[JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE];
  packet[0] = 0x01u;
  packet[1] = 0x02u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_hci_transport_send(&s_transport, 0x01u, packet, 2u));
  TEST_ASSERT_EQUAL_UINT(6u, s_fake.write_length);
  TEST_ASSERT_EQUAL_HEX8(0x01u, s_fake.write[3]);
  TEST_ASSERT_EQUAL_HEX8(0x01u, s_fake.write[4]);
  TEST_ASSERT_EQUAL_UINT(1u, snapshot().tx_command_packets);

  s_fake.write_status = HAL_EBUSY;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, jh_bluetooth_hci_transport_send(
                                       &s_transport, 0x02u, packet, 2u));
  TEST_ASSERT_EQUAL_UINT(1u, snapshot().tx_packets);
  TEST_ASSERT_EQUAL_UINT(0u, snapshot().tx_acl_packets);
}

void test_packet_handler_cannot_reenter_transport_service(void) {
  const uint8_t payload = 0u;
  queue_frame(0x04u, &payload, sizeof(payload));
  s_sink.try_reentry = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_hci_transport_open(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_hci_transport_service(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, s_sink.reentry_status);
}

void test_invalidation_closes_transport_and_preserves_failure(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_hci_transport_open(&s_transport));
  jh_bluetooth_hci_transport_invalidate(&s_transport, HAL_EHW);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        jh_bluetooth_hci_transport_service(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_EHW, snapshot().last_status);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_open_uses_factory_wifi_address_plus_one);
  RUN_TEST(test_open_propagates_controller_initialization_failure);
  RUN_TEST(test_receive_drain_is_bounded_and_resumes_next_service);
  RUN_TEST(test_flow_control_command_completions_are_recorded);
  RUN_TEST(test_malformed_and_failed_reads_propagate_hal_status);
  RUN_TEST(test_send_prepends_type_and_counts_only_successful_packets);
  RUN_TEST(test_packet_handler_cannot_reenter_transport_service);
  RUN_TEST(test_invalidation_closes_transport_and_preserves_failure);
  return UNITY_END();
}

#include "hal/hal_system.h"
#include "hal/impl/rp2040/frameworks/PubSubClient/src/PubSubClient.h"
#include "hal/impl/shared/network/mqtt/jh_pubsub_hal_client.h"
#include "utils/unity.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#ifndef HAL_ENABLE_TCP
#error "HAL_ENABLE_MQTT must propagate HAL_ENABLE_TCP"
#endif

struct hal_tcp_socket_impl_t {
  bool allocated;
  bool connected;
};

static hal_tcp_socket_impl_t s_socket = {};
static unsigned long s_now_ms = 0UL;
static hal_status_t s_resolve_status = HAL_OK;
static hal_status_t s_connect_status = HAL_OK;
static size_t s_max_send_chunk = std::numeric_limits<size_t>::max();
static size_t s_max_receive_chunk = std::numeric_limits<size_t>::max();
static size_t s_send_success_budget = std::numeric_limits<size_t>::max();
static unsigned s_open_count = 0u;
static unsigned s_close_count = 0u;
static unsigned s_send_call_count = 0u;
static uint32_t s_connect_timeout_ms = 0u;
static std::string s_resolved_host;
static hal_net_endpoint_t s_connected_endpoint = {};
static std::vector<uint8_t> s_transmitted;
static std::vector<uint8_t> s_received;
static size_t s_receive_offset = 0u;

static std::string s_callback_topic;
static std::vector<uint8_t> s_callback_payload;

static void reset_fake_transport(void) {
  s_socket = {};
  s_now_ms = 0UL;
  s_resolve_status = HAL_OK;
  s_connect_status = HAL_OK;
  s_max_send_chunk = std::numeric_limits<size_t>::max();
  s_max_receive_chunk = std::numeric_limits<size_t>::max();
  s_send_success_budget = std::numeric_limits<size_t>::max();
  s_open_count = 0u;
  s_close_count = 0u;
  s_send_call_count = 0u;
  s_connect_timeout_ms = 0u;
  s_resolved_host.clear();
  s_connected_endpoint = {};
  s_transmitted.clear();
  s_received.clear();
  s_receive_offset = 0u;
  s_callback_topic.clear();
  s_callback_payload.clear();
}

static void inject_receive(const uint8_t *data, size_t size) {
  s_received.insert(s_received.end(), data, data + size);
}

unsigned long millis(void) { return s_now_ms++; }

void yield(void) { ++s_now_ms; }

uint32_t hal_millis(void) { return (uint32_t)s_now_ms; }

void hal_delay_ms(uint32_t delay_ms) { s_now_ms += delay_ms; }

void hal_idle(void) {}

extern "C" void hal_derr(const char *, ...) {}

hal_status_t hal_net_resolve_ipv4_ex(const char *host,
                                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (host == NULL || out_addr == NULL) {
    return HAL_EINVAL;
  }
  s_resolved_host = host;
  if (s_resolve_status != HAL_OK) {
    return s_resolve_status;
  }
  out_addr[0] = 10u;
  out_addr[1] = 20u;
  out_addr[2] = 30u;
  out_addr[3] = 40u;
  return HAL_OK;
}

hal_status_t hal_tcp_socket_open_ex(hal_tcp_socket_t *out_socket) {
  if (out_socket == NULL) {
    return HAL_EINVAL;
  }
  *out_socket = NULL;
  if (s_socket.allocated) {
    return HAL_ENOMEM;
  }
  s_socket.allocated = true;
  s_socket.connected = false;
  ++s_open_count;
  *out_socket = &s_socket;
  return HAL_OK;
}

hal_status_t hal_tcp_socket_connect_ex(hal_tcp_socket_t socket,
                                       const hal_net_endpoint_t *remote,
                                       uint32_t timeout_ms) {
  if (socket != &s_socket || !s_socket.allocated || remote == NULL) {
    return HAL_EINVAL;
  }
  s_connected_endpoint = *remote;
  s_connect_timeout_ms = timeout_ms;
  if (s_connect_status != HAL_OK) {
    return s_connect_status;
  }
  s_socket.connected = true;
  return HAL_OK;
}

hal_status_t hal_tcp_socket_send_ex(hal_tcp_socket_t socket, const void *data,
                                    size_t size, size_t *out_sent) {
  if (out_sent == NULL) {
    return HAL_EINVAL;
  }
  *out_sent = 0u;
  if (socket != &s_socket || !s_socket.allocated || !s_socket.connected) {
    return HAL_ESTATE;
  }
  if (size > 0u && data == NULL) {
    return HAL_EINVAL;
  }
  ++s_send_call_count;
  if (s_transmitted.size() >= s_send_success_budget) {
    return HAL_EAGAIN;
  }

  size_t count = std::min(size, s_max_send_chunk);
  count = std::min(count, s_send_success_budget - s_transmitted.size());
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  s_transmitted.insert(s_transmitted.end(), bytes, bytes + count);
  *out_sent = count;
  return count > 0u || size == 0u ? HAL_OK : HAL_EAGAIN;
}

hal_status_t hal_tcp_socket_recv_ex(hal_tcp_socket_t socket, void *buffer,
                                    size_t max_size, uint32_t,
                                    size_t *out_received) {
  if (out_received == NULL) {
    return HAL_EINVAL;
  }
  *out_received = 0u;
  if (socket != &s_socket || !s_socket.allocated) {
    return HAL_ESTATE;
  }
  if (max_size > 0u && buffer == NULL) {
    return HAL_EINVAL;
  }

  const size_t available = s_received.size() - s_receive_offset;
  size_t count = std::min(max_size, available);
  count = std::min(count, s_max_receive_chunk);
  if (count > 0u) {
    std::copy_n(s_received.data() + s_receive_offset, count,
                static_cast<uint8_t *>(buffer));
    s_receive_offset += count;
    *out_received = count;
  }
  return HAL_OK;
}

bool hal_tcp_socket_can_recv(hal_tcp_socket_t socket) {
  return socket == &s_socket && s_socket.allocated &&
         s_receive_offset < s_received.size();
}

bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket) {
  return socket == &s_socket && s_socket.allocated &&
         (s_socket.connected || s_receive_offset < s_received.size());
}

void hal_tcp_socket_close(hal_tcp_socket_t socket) {
  if (socket == &s_socket && s_socket.allocated) {
    s_socket.allocated = false;
    s_socket.connected = false;
    ++s_close_count;
  }
}

void setUp(void) { reset_fake_transport(); }

void tearDown(void) {}

static void mqtt_callback(char *topic, uint8_t *payload, unsigned int length) {
  s_callback_topic = topic == NULL ? "" : topic;
  s_callback_payload.assign(payload, payload + length);
}

void test_adapter_resolves_connects_and_preserves_fragmented_reads(void) {
  JHPubSubHalClient client;
  client.setTimeout(25UL);
  TEST_ASSERT_EQUAL_INT(1, client.connect("broker.test", 1883u));
  TEST_ASSERT_EQUAL_STRING("broker.test", s_resolved_host.c_str());
  TEST_ASSERT_EQUAL_UINT8(10u, s_connected_endpoint.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(20u, s_connected_endpoint.addr[1]);
  TEST_ASSERT_EQUAL_UINT8(30u, s_connected_endpoint.addr[2]);
  TEST_ASSERT_EQUAL_UINT8(40u, s_connected_endpoint.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(1883u, s_connected_endpoint.port);
  TEST_ASSERT_EQUAL_UINT32(25u, s_connect_timeout_ms);

  const uint8_t outgoing[] = {1u, 2u, 3u, 4u, 5u};
  s_max_send_chunk = 2u;
  TEST_ASSERT_EQUAL_UINT(sizeof(outgoing),
                         client.write(outgoing, sizeof(outgoing)));
  TEST_ASSERT_EQUAL_UINT(3u, s_send_call_count);
  TEST_ASSERT_EQUAL_MEMORY(outgoing, s_transmitted.data(), sizeof(outgoing));

  const uint8_t incoming[] = {0x11u, 0x22u, 0x33u};
  inject_receive(incoming, sizeof(incoming));
  s_max_receive_chunk = 1u;
  TEST_ASSERT_EQUAL_INT(1, client.available());
  TEST_ASSERT_EQUAL_HEX8(0x11u, client.peek());
  TEST_ASSERT_EQUAL_INT(1, client.available());

  uint8_t buffer[2] = {};
  TEST_ASSERT_EQUAL_INT(2, client.read(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_HEX8(0x11u, buffer[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22u, buffer[1]);
  TEST_ASSERT_EQUAL_HEX8(0x33u, client.read());
  TEST_ASSERT_EQUAL_INT(0, client.available());

  client.stop();
  TEST_ASSERT_EQUAL_UINT(1u, s_close_count);
  TEST_ASSERT_FALSE((bool)client);
}

void test_adapter_partial_write_timeout_closes_stream(void) {
  JHPubSubHalClient client;
  client.setTimeout(3UL);
  TEST_ASSERT_EQUAL_INT(1, client.connect(IPAddress(1u, 2u, 3u, 4u), 1883u));

  s_max_send_chunk = 1u;
  s_send_success_budget = 1u;
  const uint8_t packet[] = {0x10u, 0x01u, 0x00u};
  TEST_ASSERT_EQUAL_UINT(1u, client.write(packet, sizeof(packet)));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3u, (uint32_t)s_now_ms);
  TEST_ASSERT_EQUAL_UINT(1u, s_close_count);
  TEST_ASSERT_EQUAL_INT(0, client.connected());
}

void test_adapter_remote_close_and_connect_failure_allow_reconnect(void) {
  JHPubSubHalClient client;
  TEST_ASSERT_EQUAL_INT(1, client.connect("first.test", 1883u));
  s_socket.connected = false;
  TEST_ASSERT_EQUAL_INT(0, client.connected());

  TEST_ASSERT_EQUAL_INT(1, client.connect("second.test", 1884u));
  TEST_ASSERT_EQUAL_UINT(2u, s_open_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_close_count);
  TEST_ASSERT_EQUAL_STRING("second.test", s_resolved_host.c_str());
  TEST_ASSERT_EQUAL_UINT16(1884u, s_connected_endpoint.port);

  s_connect_status = HAL_ETIMEOUT;
  TEST_ASSERT_EQUAL_INT(0, client.connect("offline.test", 1885u));
  TEST_ASSERT_EQUAL_UINT(3u, s_open_count);
  TEST_ASSERT_EQUAL_UINT(3u, s_close_count);
  TEST_ASSERT_EQUAL_INT(0, client.connected());
}

void test_pubsub_uses_hal_transport_for_partial_writes_and_fragmented_publish(
    void) {
  JHPubSubHalClient network;
  PubSubClient mqtt(network);
  mqtt.setServer("mqtt.test", 1883u);
  mqtt.setCallback(mqtt_callback);
  s_max_send_chunk = 2u;
  s_max_receive_chunk = 1u;

  const uint8_t connack[] = {0x20u, 0x02u, 0x00u, 0x00u};
  inject_receive(connack, sizeof(connack));
  TEST_ASSERT_TRUE(mqtt.connect("node-01"));
  TEST_ASSERT_EQUAL_INT(MQTT_CONNECTED, mqtt.state());
  TEST_ASSERT_EQUAL_HEX8(MQTTCONNECT, s_transmitted[0] & 0xf0u);
  TEST_ASSERT_GREATER_THAN_UINT(1u, s_send_call_count);

  s_transmitted.clear();
  s_send_call_count = 0u;
  const uint8_t payload[] = {'o', 'k'};
  TEST_ASSERT_TRUE(mqtt.publish("telemetry", payload, sizeof(payload), false));
  TEST_ASSERT_EQUAL_HEX8(MQTTPUBLISH, s_transmitted[0] & 0xf0u);
  TEST_ASSERT_GREATER_THAN_UINT(1u, s_send_call_count);

  const uint8_t incoming_publish[] = {0x30u, 0x07u, 0x00u, 0x03u, 'a',
                                      '/',   'b',   'o',   'k'};
  inject_receive(incoming_publish, sizeof(incoming_publish));
  TEST_ASSERT_TRUE(mqtt.loop());
  TEST_ASSERT_EQUAL_STRING("a/b", s_callback_topic.c_str());
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), s_callback_payload.size());
  TEST_ASSERT_EQUAL_MEMORY(payload, s_callback_payload.data(), sizeof(payload));
}

void test_pubsub_connack_timeout_closes_and_reconnects(void) {
  JHPubSubHalClient network;
  PubSubClient mqtt(network);
  mqtt.setServer("mqtt.test", 1883u);
  mqtt.setSocketTimeout(1u);

  TEST_ASSERT_FALSE(mqtt.connect("node-timeout"));
  TEST_ASSERT_EQUAL_INT(MQTT_CONNECTION_TIMEOUT, mqtt.state());
  TEST_ASSERT_EQUAL_UINT(1u, s_close_count);
  TEST_ASSERT_EQUAL_INT(0, network.connected());

  const uint8_t connack[] = {0x20u, 0x02u, 0x00u, 0x00u};
  inject_receive(connack, sizeof(connack));
  TEST_ASSERT_TRUE(mqtt.connect("node-retry"));
  TEST_ASSERT_EQUAL_INT(MQTT_CONNECTED, mqtt.state());
  TEST_ASSERT_EQUAL_UINT(2u, s_open_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_adapter_resolves_connects_and_preserves_fragmented_reads);
  RUN_TEST(test_adapter_partial_write_timeout_closes_stream);
  RUN_TEST(test_adapter_remote_close_and_connect_failure_allow_reconnect);
  RUN_TEST(
      test_pubsub_uses_hal_transport_for_partial_writes_and_fragmented_publish);
  RUN_TEST(test_pubsub_connack_timeout_closes_and_reconnects);
  return UNITY_END();
}

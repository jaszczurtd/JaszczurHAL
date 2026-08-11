#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/mqtt/hal_mqtt.h"
#include "support/tls_test_helpers.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

static bool s_callback_called = false;
static char s_callback_topic[128] = {0};
static uint8_t s_callback_payload[512] = {0};
static uint16_t s_callback_payload_len = 0;

#ifdef HAL_ENABLE_TLS
static hal_status_t mqtt_test_time(void *, uint64_t *out_seconds) {
  *out_seconds = 1704067200u;
  return HAL_OK;
}

static hal_status_t mqtt_test_entropy(void *, void *buffer, size_t length) {
  memset(buffer, 0x5A, length);
  return HAL_OK;
}
#endif

static void on_message(const char *topic, const uint8_t *payload,
                       uint16_t length, void *user) {
  (void)user;

  s_callback_called = true;
  snprintf(s_callback_topic, sizeof(s_callback_topic), "%s",
           topic ? topic : "");

  s_callback_payload_len = length;
  if (s_callback_payload_len > sizeof(s_callback_payload)) {
    s_callback_payload_len = (uint16_t)sizeof(s_callback_payload);
  }

  if (payload && s_callback_payload_len > 0u) {
    memcpy(s_callback_payload, payload, s_callback_payload_len);
  }
}

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_mqtt_reset();

  s_callback_called = false;
  memset(s_callback_topic, 0, sizeof(s_callback_topic));
  memset(s_callback_payload, 0, sizeof(s_callback_payload));
  s_callback_payload_len = 0;
}

void tearDown(void) {}

void test_server_connect_publish_subscribe_and_disconnect(void) {
  const uint8_t payload[] = {0xAA, 0xBB, 0xCC};

  TEST_ASSERT_TRUE(hal_mqtt_set_server("broker.example", 1883));
  TEST_ASSERT_EQUAL_STRING("broker.example", hal_mock_mqtt_get_server_host());
  TEST_ASSERT_EQUAL_UINT16(1883u, hal_mock_mqtt_get_server_port());

  TEST_ASSERT_TRUE(hal_mqtt_connect("node-01"));
  TEST_ASSERT_TRUE(hal_mqtt_connected());
  TEST_ASSERT_EQUAL_INT(0, hal_mqtt_state());

  TEST_ASSERT_TRUE(
      hal_mqtt_publish("telemetry/raw", payload, sizeof(payload), true));
  TEST_ASSERT_EQUAL_STRING("telemetry/raw",
                           hal_mock_mqtt_get_last_publish_topic());
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(payload),
                           hal_mock_mqtt_get_last_publish_len());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      payload, hal_mock_mqtt_get_last_publish_payload(), sizeof(payload));
  TEST_ASSERT_TRUE(hal_mock_mqtt_get_last_publish_retained());

  TEST_ASSERT_TRUE(hal_mqtt_publish_str("telemetry/text", "hello", false));
  TEST_ASSERT_EQUAL_STRING("telemetry/text",
                           hal_mock_mqtt_get_last_publish_topic());
  TEST_ASSERT_FALSE(hal_mock_mqtt_get_last_publish_retained());

  TEST_ASSERT_TRUE(hal_mqtt_subscribe("cmd/#", 1));
  TEST_ASSERT_EQUAL_STRING("cmd/#", hal_mock_mqtt_get_last_subscribe_topic());
  TEST_ASSERT_EQUAL_UINT8(1u, hal_mock_mqtt_get_last_subscribe_qos());

  TEST_ASSERT_TRUE(hal_mqtt_unsubscribe("cmd/#"));
  TEST_ASSERT_EQUAL_STRING("cmd/#", hal_mock_mqtt_get_last_unsubscribe_topic());

  hal_mqtt_disconnect();
  TEST_ASSERT_FALSE(hal_mqtt_connected());
  TEST_ASSERT_EQUAL_INT(-1, hal_mqtt_state());
}

void test_settings_and_callback_dispatch(void) {
  const uint8_t msg[] = {'O', 'K'};

  TEST_ASSERT_TRUE(hal_mqtt_set_server("broker", 1883));
  TEST_ASSERT_TRUE(hal_mqtt_connect("node-02"));

  TEST_ASSERT_TRUE(hal_mqtt_set_buffer_size(384u));
  TEST_ASSERT_EQUAL_UINT16(384u, hal_mqtt_get_buffer_size());

  TEST_ASSERT_TRUE(hal_mqtt_set_keepalive(30u));
  TEST_ASSERT_TRUE(hal_mqtt_set_socket_timeout(9u));
  TEST_ASSERT_EQUAL_UINT16(30u, hal_mock_mqtt_get_keepalive());
  TEST_ASSERT_EQUAL_UINT16(9u, hal_mock_mqtt_get_socket_timeout());

  TEST_ASSERT_TRUE(hal_mqtt_set_callback(on_message, NULL));

  hal_mock_mqtt_inject_message("cmd/ping", msg, (uint16_t)sizeof(msg));
  TEST_ASSERT_TRUE(hal_mqtt_loop());

  TEST_ASSERT_TRUE(s_callback_called);
  TEST_ASSERT_EQUAL_STRING("cmd/ping", s_callback_topic);
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(msg), s_callback_payload_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, s_callback_payload, sizeof(msg));
}

void test_invalid_inputs_and_failed_connect_are_rejected(void) {
  TEST_ASSERT_FALSE(hal_mqtt_connect("node-without-server"));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_mqtt_set_server(NULL, 1883));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_mqtt_set_server("broker", 0));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  TEST_ASSERT_TRUE(hal_mqtt_set_server("broker", 1883));
  hal_mock_mqtt_set_connect_result(false);
  TEST_ASSERT_FALSE(hal_mqtt_connect("node-03"));

  hal_mock_mqtt_set_connected(true);
  TEST_ASSERT_FALSE(hal_mqtt_subscribe("cmd/#", 2));
  TEST_ASSERT_FALSE(hal_mqtt_publish("telemetry", NULL, 1, false));
  TEST_ASSERT_FALSE(hal_mqtt_publish_str("telemetry", NULL, false));
}

#ifdef HAL_ENABLE_TLS
void test_tls_configuration_is_fail_closed_and_plaintext_can_be_restored(void) {
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, hal_mqtt_configure_tls_ex(NULL));

  hal_tls_trust_anchor_t anchor = jh_test_tls_rsa_anchor();
  hal_tls_security_config_t security =
      jh_test_tls_security_config(&anchor, mqtt_test_time, mqtt_test_entropy);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mqtt_configure_tls_ex(&security));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mqtt_disable_tls_ex());
  TEST_ASSERT_FALSE(hal_mqtt_connected());
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_server_connect_publish_subscribe_and_disconnect);
  RUN_TEST(test_settings_and_callback_dispatch);
  RUN_TEST(test_invalid_inputs_and_failed_connect_are_rejected);
#ifdef HAL_ENABLE_TLS
  RUN_TEST(test_tls_configuration_is_fail_closed_and_plaintext_can_be_restored);
#endif
  return UNITY_END();
}

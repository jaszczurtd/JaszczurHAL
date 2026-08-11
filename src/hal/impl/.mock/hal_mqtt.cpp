#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_MQTT

#include "hal/network/mqtt/hal_mqtt.h"
#include "hal/serial/hal_serial.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

#define MOCK_MQTT_HOST_BUF_SIZE 128u
#define MOCK_MQTT_TOPIC_BUF_SIZE 128u
#define MOCK_MQTT_PAYLOAD_BUF_SIZE 512u

static struct {
  bool server_configured;
  char server_host[MOCK_MQTT_HOST_BUF_SIZE];
  uint16_t server_port;

  bool connected;
  int state;

  bool connect_result;
  bool loop_result;

  uint16_t buffer_size;
  uint16_t keepalive;
  uint16_t socket_timeout;
#ifdef HAL_ENABLE_TLS
  bool tls_enabled;
#endif

  hal_mqtt_message_callback_t callback;
  void *callback_user;

  bool pending_message;
  char pending_topic[MOCK_MQTT_TOPIC_BUF_SIZE];
  uint8_t pending_payload[MOCK_MQTT_PAYLOAD_BUF_SIZE];
  uint16_t pending_len;

  char last_publish_topic[MOCK_MQTT_TOPIC_BUF_SIZE];
  uint8_t last_publish_payload[MOCK_MQTT_PAYLOAD_BUF_SIZE];
  uint16_t last_publish_len;
  bool last_publish_retained;

  char last_subscribe_topic[MOCK_MQTT_TOPIC_BUF_SIZE];
  uint8_t last_subscribe_qos;

  char last_unsubscribe_topic[MOCK_MQTT_TOPIC_BUF_SIZE];
} s_mqtt;

static hal_status_t validate_non_empty(const char *value, const char *fn,
                                       const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return HAL_EINVAL;
  }
  return HAL_OK;
}

void hal_mock_mqtt_reset(void) {
  memset(&s_mqtt, 0, sizeof(s_mqtt));

  s_mqtt.connect_result = true;
  s_mqtt.loop_result = true;
  s_mqtt.state = -1;
  s_mqtt.buffer_size = 256u;
  s_mqtt.keepalive = 15u;
  s_mqtt.socket_timeout = 15u;
}

hal_status_t hal_mqtt_set_server_ex(const char *host, uint16_t port) {
  hal_status_t status =
      validate_non_empty(host, "hal_mqtt_set_server_ex", "host");
  if (status != HAL_OK) {
    return status;
  }
  if (port == 0u) {
    hal_derr("hal_mqtt_set_server_ex: port must be > 0");
    return HAL_EINVAL;
  }

  snprintf(s_mqtt.server_host, sizeof(s_mqtt.server_host), "%s", host);
  s_mqtt.server_port = port;
  s_mqtt.server_configured = true;
  return HAL_OK;
}

bool hal_mqtt_set_server(const char *host, uint16_t port) {
  return hal_status_to_bool(hal_mqtt_set_server_ex(host, port));
}

hal_status_t hal_mqtt_set_callback_ex(hal_mqtt_message_callback_t callback,
                                      void *user) {
  s_mqtt.callback = callback;
  s_mqtt.callback_user = user;
  return HAL_OK;
}

bool hal_mqtt_set_callback(hal_mqtt_message_callback_t callback, void *user) {
  return hal_status_to_bool(hal_mqtt_set_callback_ex(callback, user));
}

hal_status_t hal_mqtt_set_keepalive_ex(uint16_t keepalive_s) {
  if (keepalive_s == 0u) {
    hal_derr("hal_mqtt_set_keepalive_ex: keepalive_s must be > 0");
    return HAL_EINVAL;
  }
  s_mqtt.keepalive = keepalive_s;
  return HAL_OK;
}

bool hal_mqtt_set_keepalive(uint16_t keepalive_s) {
  return hal_status_to_bool(hal_mqtt_set_keepalive_ex(keepalive_s));
}

hal_status_t hal_mqtt_set_socket_timeout_ex(uint16_t timeout_s) {
  if (timeout_s == 0u) {
    hal_derr("hal_mqtt_set_socket_timeout_ex: timeout_s must be > 0");
    return HAL_EINVAL;
  }
  s_mqtt.socket_timeout = timeout_s;
  return HAL_OK;
}

bool hal_mqtt_set_socket_timeout(uint16_t timeout_s) {
  return hal_status_to_bool(hal_mqtt_set_socket_timeout_ex(timeout_s));
}

hal_status_t hal_mqtt_set_buffer_size_ex(uint16_t size) {
  if (size == 0u) {
    hal_derr("hal_mqtt_set_buffer_size_ex: size must be > 0");
    return HAL_EINVAL;
  }
  s_mqtt.buffer_size = size;
  return HAL_OK;
}

bool hal_mqtt_set_buffer_size(uint16_t size) {
  return hal_status_to_bool(hal_mqtt_set_buffer_size_ex(size));
}

#ifdef HAL_ENABLE_TLS
hal_status_t
hal_mqtt_configure_tls_ex(const hal_tls_security_config_t *security) {
  if (security == NULL || security->trust_anchors == NULL ||
      security->trust_anchor_count == 0u || security->get_time == NULL ||
      security->get_entropy == NULL) {
    return HAL_ECONFIG;
  }
  s_mqtt.tls_enabled = true;
  return HAL_OK;
}

hal_status_t hal_mqtt_disable_tls_ex(void) {
  s_mqtt.connected = false;
  s_mqtt.tls_enabled = false;
  return HAL_OK;
}
#endif

uint16_t hal_mqtt_get_buffer_size(void) { return s_mqtt.buffer_size; }

hal_status_t hal_mqtt_connect_ex(const char *client_id) {
  hal_status_t status =
      validate_non_empty(client_id, "hal_mqtt_connect_ex", "client_id");
  if (status != HAL_OK) {
    return status;
  }
  if (!s_mqtt.server_configured) {
    hal_derr("hal_mqtt_connect_ex: server is not configured");
    return HAL_EUNINIT;
  }

  s_mqtt.connected = s_mqtt.connect_result;
  s_mqtt.state = s_mqtt.connected ? 0 : -2;
  return s_mqtt.connected ? HAL_OK : HAL_EIO;
}

bool hal_mqtt_connect(const char *client_id) {
  return hal_status_to_bool(hal_mqtt_connect_ex(client_id));
}

hal_status_t hal_mqtt_connect_auth_ex(const char *client_id, const char *user,
                                      const char *pass) {
  (void)pass;
  hal_status_t status =
      validate_non_empty(client_id, "hal_mqtt_connect_auth_ex", "client_id");
  if (status != HAL_OK) {
    return status;
  }
  status = validate_non_empty(user, "hal_mqtt_connect_auth_ex", "user");
  if (status != HAL_OK) {
    return status;
  }
  if (pass == NULL) {
    hal_derr("hal_mqtt_connect_auth_ex: pass is NULL");
    return HAL_EINVAL;
  }
  if (!s_mqtt.server_configured) {
    hal_derr("hal_mqtt_connect_auth_ex: server is not configured");
    return HAL_EUNINIT;
  }

  s_mqtt.connected = s_mqtt.connect_result;
  s_mqtt.state = s_mqtt.connected ? 0 : -2;
  return s_mqtt.connected ? HAL_OK : HAL_EIO;
}

bool hal_mqtt_connect_auth(const char *client_id, const char *user,
                           const char *pass) {
  return hal_status_to_bool(hal_mqtt_connect_auth_ex(client_id, user, pass));
}

void hal_mqtt_disconnect(void) {
  s_mqtt.connected = false;
  s_mqtt.state = -1;
}

bool hal_mqtt_connected(void) { return s_mqtt.connected; }

int hal_mqtt_state(void) { return s_mqtt.state; }

hal_status_t hal_mqtt_loop_ex(void) {
  if (s_mqtt.pending_message) {
    if (s_mqtt.callback) {
      s_mqtt.callback(s_mqtt.pending_topic, s_mqtt.pending_payload,
                      s_mqtt.pending_len, s_mqtt.callback_user);
    }
    s_mqtt.pending_message = false;
    s_mqtt.pending_len = 0u;
  }

  return s_mqtt.loop_result ? HAL_OK : HAL_EIO;
}

bool hal_mqtt_loop(void) { return hal_status_to_bool(hal_mqtt_loop_ex()); }

hal_status_t hal_mqtt_publish_ex(const char *topic, const uint8_t *payload,
                                 uint16_t payload_len, bool retained) {
  hal_status_t status =
      validate_non_empty(topic, "hal_mqtt_publish_ex", "topic");
  if (status != HAL_OK) {
    return status;
  }
  if (payload_len > 0u && payload == NULL) {
    hal_derr("hal_mqtt_publish_ex: payload is NULL while payload_len > 0");
    return HAL_EINVAL;
  }
  if (!s_mqtt.connected) {
    return HAL_ESTATE;
  }

  snprintf(s_mqtt.last_publish_topic, sizeof(s_mqtt.last_publish_topic), "%s",
           topic);

  s_mqtt.last_publish_len = payload_len;
  if (s_mqtt.last_publish_len > MOCK_MQTT_PAYLOAD_BUF_SIZE) {
    s_mqtt.last_publish_len = MOCK_MQTT_PAYLOAD_BUF_SIZE;
  }
  if (payload && s_mqtt.last_publish_len > 0u) {
    memcpy(s_mqtt.last_publish_payload, payload, s_mqtt.last_publish_len);
  }

  s_mqtt.last_publish_retained = retained;
  return HAL_OK;
}

bool hal_mqtt_publish(const char *topic, const uint8_t *payload,
                      uint16_t payload_len, bool retained) {
  return hal_status_to_bool(
      hal_mqtt_publish_ex(topic, payload, payload_len, retained));
}

hal_status_t hal_mqtt_publish_str_ex(const char *topic, const char *payload,
                                     bool retained) {
  if (!payload) {
    hal_derr("hal_mqtt_publish_str_ex: payload is NULL");
    return HAL_EINVAL;
  }

  const size_t len = strnlen(payload, MOCK_MQTT_PAYLOAD_BUF_SIZE);
  return hal_mqtt_publish_ex(topic, (const uint8_t *)payload, (uint16_t)len,
                             retained);
}

bool hal_mqtt_publish_str(const char *topic, const char *payload,
                          bool retained) {
  return hal_status_to_bool(hal_mqtt_publish_str_ex(topic, payload, retained));
}

hal_status_t hal_mqtt_subscribe_ex(const char *topic, uint8_t qos) {
  hal_status_t status =
      validate_non_empty(topic, "hal_mqtt_subscribe_ex", "topic");
  if (status != HAL_OK) {
    return status;
  }
  if (qos > 1u) {
    hal_derr("hal_mqtt_subscribe_ex: qos must be 0 or 1");
    return HAL_EINVAL;
  }
  if (!s_mqtt.connected) {
    return HAL_ESTATE;
  }

  snprintf(s_mqtt.last_subscribe_topic, sizeof(s_mqtt.last_subscribe_topic),
           "%s", topic);
  s_mqtt.last_subscribe_qos = qos;
  return HAL_OK;
}

bool hal_mqtt_subscribe(const char *topic, uint8_t qos) {
  return hal_status_to_bool(hal_mqtt_subscribe_ex(topic, qos));
}

hal_status_t hal_mqtt_unsubscribe_ex(const char *topic) {
  hal_status_t status =
      validate_non_empty(topic, "hal_mqtt_unsubscribe_ex", "topic");
  if (status != HAL_OK) {
    return status;
  }
  if (!s_mqtt.connected) {
    return HAL_ESTATE;
  }

  snprintf(s_mqtt.last_unsubscribe_topic, sizeof(s_mqtt.last_unsubscribe_topic),
           "%s", topic);
  return HAL_OK;
}

bool hal_mqtt_unsubscribe(const char *topic) {
  return hal_status_to_bool(hal_mqtt_unsubscribe_ex(topic));
}

void hal_mock_mqtt_set_connect_result(bool result) {
  s_mqtt.connect_result = result;
}

void hal_mock_mqtt_set_loop_result(bool result) { s_mqtt.loop_result = result; }

void hal_mock_mqtt_set_connected(bool connected) {
  s_mqtt.connected = connected;
}

void hal_mock_mqtt_set_state(int state) { s_mqtt.state = state; }

void hal_mock_mqtt_inject_message(const char *topic, const uint8_t *payload,
                                  uint16_t length) {
  const char *safe_topic = topic ? topic : "";
  snprintf(s_mqtt.pending_topic, sizeof(s_mqtt.pending_topic), "%s",
           safe_topic);

  s_mqtt.pending_len = length;
  if (s_mqtt.pending_len > MOCK_MQTT_PAYLOAD_BUF_SIZE) {
    s_mqtt.pending_len = MOCK_MQTT_PAYLOAD_BUF_SIZE;
  }

  if (payload && s_mqtt.pending_len > 0u) {
    memcpy(s_mqtt.pending_payload, payload, s_mqtt.pending_len);
  }
  s_mqtt.pending_message = true;
}

const char *hal_mock_mqtt_get_server_host(void) { return s_mqtt.server_host; }

uint16_t hal_mock_mqtt_get_server_port(void) { return s_mqtt.server_port; }

const char *hal_mock_mqtt_get_last_publish_topic(void) {
  return s_mqtt.last_publish_topic;
}

const uint8_t *hal_mock_mqtt_get_last_publish_payload(void) {
  return s_mqtt.last_publish_payload;
}

uint16_t hal_mock_mqtt_get_last_publish_len(void) {
  return s_mqtt.last_publish_len;
}

bool hal_mock_mqtt_get_last_publish_retained(void) {
  return s_mqtt.last_publish_retained;
}

const char *hal_mock_mqtt_get_last_subscribe_topic(void) {
  return s_mqtt.last_subscribe_topic;
}

uint8_t hal_mock_mqtt_get_last_subscribe_qos(void) {
  return s_mqtt.last_subscribe_qos;
}

const char *hal_mock_mqtt_get_last_unsubscribe_topic(void) {
  return s_mqtt.last_unsubscribe_topic;
}

uint16_t hal_mock_mqtt_get_keepalive(void) { return s_mqtt.keepalive; }

uint16_t hal_mock_mqtt_get_socket_timeout(void) {
  return s_mqtt.socket_timeout;
}

#endif /* HAL_ENABLE_MQTT */
#endif // HAL_TARGET_IS_MOCK

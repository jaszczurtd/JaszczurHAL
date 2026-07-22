#include "../../../../hal_target.h"
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474
#include "../../../../hal_config.h"

#ifdef HAL_ENABLE_MQTT

#include "../../../../hal_mqtt.h"
#include "../../../../hal_serial.h"
#include "../../../../hal_sync.h"
#include "../../frameworks/PubSubClient/src/PubSubClient.h"
#include "../../hal_mutex_once.h"
#include "jh_pubsub_hal_client.h"

#include <stdio.h>
#include <string.h>

#define HAL_MQTT_HOST_BUF_SIZE 256u
#define HAL_MQTT_TOPIC_BUF_SIZE 512u
#define HAL_MQTT_PAYLOAD_BUF_SIZE 2048u
#define HAL_MQTT_RX_QUEUE_DEPTH 8u

static JHPubSubHalClient s_network_client;
static PubSubClient s_client(s_network_client);

static hal_mutex_t s_mqtt_mutex = NULL;
static bool s_server_configured = false;
static char s_server_host[HAL_MQTT_HOST_BUF_SIZE] = {};

static hal_mqtt_message_callback_t s_user_callback = NULL;
static void *s_user_callback_user = NULL;

// Bounded FIFO of inbound MQTT messages. PubSubClient may deliver several
// publishes inside a single s_client.loop() call; the original single-slot
// pending buffer overwrote earlier messages, which produced "module stopped
// responding" symptoms during MQTT bursts. The queue is drained from
// hal_mqtt_loop() one message at a time with the user callback invoked
// outside the module mutex.
typedef struct {
  char topic[HAL_MQTT_TOPIC_BUF_SIZE];
  uint8_t payload[HAL_MQTT_PAYLOAD_BUF_SIZE];
  uint16_t length;
} hal_mqtt_rx_slot_t;

static hal_mqtt_rx_slot_t s_rx_queue[HAL_MQTT_RX_QUEUE_DEPTH];
static uint8_t s_rx_head = 0; // next slot to read
static uint8_t s_rx_tail = 0; // next slot to write
static uint8_t s_rx_count = 0;
static uint32_t s_rx_overflow_count = 0;

static inline void mqtt_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_mqtt_mutex);
}

static hal_status_t validate_non_empty(const char *value, const char *fn,
                                       const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return HAL_EINVAL;
  }
  return HAL_OK;
}

static void mqtt_internal_callback(char *topic, uint8_t *payload,
                                   unsigned int length) {
  const char *safe_topic = topic ? topic : "";
  const size_t topic_len = strnlen(safe_topic, HAL_MQTT_TOPIC_BUF_SIZE - 1u);

  uint16_t copy_len = (uint16_t)length;
  if (copy_len > HAL_MQTT_PAYLOAD_BUF_SIZE) {
    hal_derr("mqtt_internal_callback: payload length %u exceeds buffer size, "
             "truncating",
             (unsigned)length);
    copy_len = HAL_MQTT_PAYLOAD_BUF_SIZE;
  }

  if (s_rx_count >= HAL_MQTT_RX_QUEUE_DEPTH) {
    // Queue full: drop newest (preserve earliest message ordering) and log.
    s_rx_overflow_count++;
    hal_derr("mqtt_internal_callback: RX queue full (depth=%u), dropping "
             "topic='%s' (overflow=%lu)",
             (unsigned)HAL_MQTT_RX_QUEUE_DEPTH, safe_topic,
             (unsigned long)s_rx_overflow_count);
    return;
  }

  hal_mqtt_rx_slot_t *slot = &s_rx_queue[s_rx_tail];
  memcpy(slot->topic, safe_topic, topic_len);
  slot->topic[topic_len] = '\0';

  if (payload && copy_len > 0u) {
    memcpy(slot->payload, payload, copy_len);
  }
  slot->length = copy_len;

  s_rx_tail = (uint8_t)((s_rx_tail + 1u) % HAL_MQTT_RX_QUEUE_DEPTH);
  s_rx_count++;
}

static inline void mqtt_bind_callback(void) {
  s_client.setCallback(mqtt_internal_callback);
}

hal_status_t hal_mqtt_set_server_ex(const char *host, uint16_t port) {
  hal_status_t status =
      validate_non_empty(host, "hal_mqtt_set_server_ex", "host");
  if (status != HAL_OK) {
    return status;
  }

  if (port == 0u) {
    hal_derr("hal_mqtt_set_server_ex: invalid broker port: %u", (unsigned)port);
    return HAL_EINVAL;
  }

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  snprintf(s_server_host, sizeof(s_server_host), "%s", host);
  s_client.setServer(s_server_host, port);
  mqtt_bind_callback();
  s_server_configured = true;

  hal_mutex_unlock(s_mqtt_mutex);
  return HAL_OK;
}

bool hal_mqtt_set_server(const char *host, uint16_t port) {
  return hal_status_to_bool(hal_mqtt_set_server_ex(host, port));
}

hal_status_t hal_mqtt_set_callback_ex(hal_mqtt_message_callback_t callback,
                                      void *user) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  s_user_callback = callback;
  s_user_callback_user = user;
  mqtt_bind_callback();

  hal_mutex_unlock(s_mqtt_mutex);
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

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  s_client.setKeepAlive(keepalive_s);

  hal_mutex_unlock(s_mqtt_mutex);
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

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  s_client.setSocketTimeout(timeout_s);
  s_network_client.set_timeout_ms((uint32_t)timeout_s * 1000u);

  hal_mutex_unlock(s_mqtt_mutex);
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

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  const bool ok = s_client.setBufferSize(size);

  hal_mutex_unlock(s_mqtt_mutex);
  if (!ok) {
    hal_derr("hal_mqtt_set_buffer_size_ex: setBufferSize(%u) failed",
             (unsigned)size);
    return HAL_EIO;
  }
  return HAL_OK;
}

bool hal_mqtt_set_buffer_size(uint16_t size) {
  return hal_status_to_bool(hal_mqtt_set_buffer_size_ex(size));
}

#ifdef HAL_ENABLE_TLS
hal_status_t
hal_mqtt_configure_tls_ex(const hal_tls_security_config_t *security) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);
  const hal_status_t status = s_network_client.configure_tls(security);
  hal_mutex_unlock(s_mqtt_mutex);
  return status;
}

hal_status_t hal_mqtt_disable_tls_ex(void) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);
  s_network_client.disable_tls();
  hal_mutex_unlock(s_mqtt_mutex);
  return HAL_OK;
}
#endif

uint16_t hal_mqtt_get_buffer_size(void) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  const uint16_t size = s_client.getBufferSize();

  hal_mutex_unlock(s_mqtt_mutex);
  return size;
}

hal_status_t hal_mqtt_connect_ex(const char *client_id) {
  hal_status_t status =
      validate_non_empty(client_id, "hal_mqtt_connect_ex", "client_id");
  if (status != HAL_OK) {
    return status;
  }

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  if (!s_server_configured) {
    hal_mutex_unlock(s_mqtt_mutex);
    hal_derr("hal_mqtt_connect_ex: server is not configured");
    return HAL_EUNINIT;
  }

  mqtt_bind_callback();
  const bool ok = s_client.connect(client_id);

  hal_mutex_unlock(s_mqtt_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_mqtt_connect(const char *client_id) {
  return hal_status_to_bool(hal_mqtt_connect_ex(client_id));
}

hal_status_t hal_mqtt_connect_auth_ex(const char *client_id, const char *user,
                                      const char *pass) {
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

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  if (!s_server_configured) {
    hal_mutex_unlock(s_mqtt_mutex);
    hal_derr("hal_mqtt_connect_auth_ex: server is not configured");
    return HAL_EUNINIT;
  }

  mqtt_bind_callback();
  const bool ok = s_client.connect(client_id, user, pass);

  hal_mutex_unlock(s_mqtt_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_mqtt_connect_auth(const char *client_id, const char *user,
                           const char *pass) {
  return hal_status_to_bool(hal_mqtt_connect_auth_ex(client_id, user, pass));
}

void hal_mqtt_disconnect(void) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  s_client.disconnect();

  hal_mutex_unlock(s_mqtt_mutex);
}

bool hal_mqtt_connected(void) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  const bool connected = s_client.connected();

  hal_mutex_unlock(s_mqtt_mutex);
  return connected;
}

int hal_mqtt_state(void) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  const int state = s_client.state();

  hal_mutex_unlock(s_mqtt_mutex);
  return state;
}

hal_status_t hal_mqtt_loop_ex(void) {
  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  mqtt_bind_callback();
  const bool ok = s_client.loop();

  hal_mqtt_message_callback_t callback = s_user_callback;
  void *callback_user = s_user_callback_user;

  hal_mutex_unlock(s_mqtt_mutex);

  // Drain the inbound queue one message at a time. The user callback is
  // invoked OUTSIDE the module mutex so it may safely re-enter publish/
  // subscribe operations.
  while (true) {
    char topic_copy[HAL_MQTT_TOPIC_BUF_SIZE] = {};
    uint8_t payload_copy[HAL_MQTT_PAYLOAD_BUF_SIZE] = {};
    uint16_t payload_len = 0;
    bool has_message = false;

    hal_mutex_lock(s_mqtt_mutex);
    if (s_rx_count > 0u) {
      hal_mqtt_rx_slot_t *slot = &s_rx_queue[s_rx_head];
      snprintf(topic_copy, sizeof(topic_copy), "%s", slot->topic);
      payload_len = slot->length;
      if (payload_len > 0u) {
        memcpy(payload_copy, slot->payload, payload_len);
      }
      s_rx_head = (uint8_t)((s_rx_head + 1u) % HAL_MQTT_RX_QUEUE_DEPTH);
      s_rx_count--;
      has_message = true;
    }
    hal_mutex_unlock(s_mqtt_mutex);

    if (!has_message)
      break;
    if (callback) {
      callback(topic_copy, payload_copy, payload_len, callback_user);
    }
  }

  return ok ? HAL_OK : HAL_EIO;
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

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  bool connected = s_client.connected();
  const bool ok = connected
                      ? s_client.publish(topic, payload, payload_len, retained)
                      : false;

  hal_mutex_unlock(s_mqtt_mutex);
  if (!connected) {
    return HAL_ESTATE;
  }
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_mqtt_publish(const char *topic, const uint8_t *payload,
                      uint16_t payload_len, bool retained) {
  return hal_status_to_bool(
      hal_mqtt_publish_ex(topic, payload, payload_len, retained));
}

hal_status_t hal_mqtt_publish_str_ex(const char *topic, const char *payload,
                                     bool retained) {
  hal_status_t status =
      validate_non_empty(topic, "hal_mqtt_publish_str_ex", "topic");
  if (status != HAL_OK) {
    return status;
  }
  if (!payload) {
    hal_derr("hal_mqtt_publish_str_ex: payload is NULL");
    return HAL_EINVAL;
  }

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  bool connected = s_client.connected();
  const bool ok =
      connected ? s_client.publish(topic, payload, retained) : false;

  hal_mutex_unlock(s_mqtt_mutex);
  if (!connected) {
    return HAL_ESTATE;
  }
  return ok ? HAL_OK : HAL_EIO;
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

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  bool connected = s_client.connected();
  const bool ok = connected ? s_client.subscribe(topic, qos) : false;

  hal_mutex_unlock(s_mqtt_mutex);
  if (!connected) {
    return HAL_ESTATE;
  }
  return ok ? HAL_OK : HAL_EIO;
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

  mqtt_ensure_mutex();
  hal_mutex_lock(s_mqtt_mutex);

  bool connected = s_client.connected();
  const bool ok = connected ? s_client.unsubscribe(topic) : false;

  hal_mutex_unlock(s_mqtt_mutex);
  if (!connected) {
    return HAL_ESTATE;
  }
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_mqtt_unsubscribe(const char *topic) {
  return hal_status_to_bool(hal_mqtt_unsubscribe_ex(topic));
}

#endif /* HAL_ENABLE_MQTT */
#endif // HAL_TARGET_IS_RP2040

#include "../../hal_config.h"

#ifdef HAL_ENABLE_MQTT

#include "../../hal_mqtt.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "drivers/PubSubClient/src/PubSubClient.h"

#include <WiFiClient.h>
#include <stdio.h>
#include <string.h>

#define HAL_MQTT_HOST_BUF_SIZE 128u
#define HAL_MQTT_TOPIC_BUF_SIZE 128u
#define HAL_MQTT_PAYLOAD_BUF_SIZE 512u

static WiFiClient s_wifi_client;
static PubSubClient s_client(s_wifi_client);

static hal_mutex_t s_mqtt_mutex = NULL;
static bool s_server_configured = false;
static char s_server_host[HAL_MQTT_HOST_BUF_SIZE] = {0};

static hal_mqtt_message_callback_t s_user_callback = NULL;
static void *s_user_callback_user = NULL;

static bool s_pending_valid = false;
static char s_pending_topic[HAL_MQTT_TOPIC_BUF_SIZE] = {0};
static uint8_t s_pending_payload[HAL_MQTT_PAYLOAD_BUF_SIZE] = {0};
static uint16_t s_pending_length = 0;

static inline void mqtt_ensure_mutex(void) {
    if (s_mqtt_mutex == NULL) {
        hal_critical_section_enter();
        if (s_mqtt_mutex == NULL) {
            s_mqtt_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

static bool validate_non_empty(const char *value, const char *fn, const char *name) {
    if (!value || value[0] == '\0') {
        hal_derr("%s: %s is NULL/empty", fn, name);
        return false;
    }
    return true;
}

static void mqtt_internal_callback(char *topic, uint8_t *payload, unsigned int length) {
    const char *safe_topic = topic ? topic : "";
    const size_t topic_len = strnlen(safe_topic, HAL_MQTT_TOPIC_BUF_SIZE - 1u);

    memcpy(s_pending_topic, safe_topic, topic_len);
    s_pending_topic[topic_len] = '\0';

    uint16_t copy_len = (uint16_t)length;
    if (copy_len > HAL_MQTT_PAYLOAD_BUF_SIZE) {
        hal_derr("mqtt_internal_callback: payload length %u exceeds buffer size, truncating",
             (unsigned)length);
        copy_len = HAL_MQTT_PAYLOAD_BUF_SIZE;
    }

    if (payload && copy_len > 0u) {
        memcpy(s_pending_payload, payload, copy_len);
    }
    s_pending_length = copy_len;
    s_pending_valid = true;
}

static inline void mqtt_bind_callback(void) {
    s_client.setCallback(mqtt_internal_callback);
}

bool hal_mqtt_set_server(const char *host, uint16_t port) {
    if (!validate_non_empty(host, "hal_mqtt_set_server", "host")) {
        return false;
    }
    if (port == 0u) {
        hal_derr("hal_mqtt_set_server: port must be > 0");
        return false;
    }

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    snprintf(s_server_host, sizeof(s_server_host), "%s", host);
    s_client.setServer(s_server_host, port);
    mqtt_bind_callback();
    s_server_configured = true;

    hal_mutex_unlock(s_mqtt_mutex);
    return true;
}

bool hal_mqtt_set_callback(hal_mqtt_message_callback_t callback, void *user) {
    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    s_user_callback = callback;
    s_user_callback_user = user;
    mqtt_bind_callback();

    hal_mutex_unlock(s_mqtt_mutex);
    return true;
}

bool hal_mqtt_set_keepalive(uint16_t keepalive_s) {
    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    s_client.setKeepAlive(keepalive_s);

    hal_mutex_unlock(s_mqtt_mutex);
    return true;
}

bool hal_mqtt_set_socket_timeout(uint16_t timeout_s) {
    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    s_client.setSocketTimeout(timeout_s);

    hal_mutex_unlock(s_mqtt_mutex);
    return true;
}

bool hal_mqtt_set_buffer_size(uint16_t size) {
    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    const bool ok = s_client.setBufferSize(size);

    hal_mutex_unlock(s_mqtt_mutex);
    if (!ok) {
        hal_derr("hal_mqtt_set_buffer_size: setBufferSize(%u) failed", (unsigned)size);
    }
    return ok;
}

uint16_t hal_mqtt_get_buffer_size(void) {
    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    const uint16_t size = s_client.getBufferSize();

    hal_mutex_unlock(s_mqtt_mutex);
    return size;
}

bool hal_mqtt_connect(const char *client_id) {
    if (!validate_non_empty(client_id, "hal_mqtt_connect", "client_id")) {
        return false;
    }

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    if (!s_server_configured) {
        hal_mutex_unlock(s_mqtt_mutex);
        hal_derr("hal_mqtt_connect: server is not configured");
        return false;
    }

    mqtt_bind_callback();
    const bool ok = s_client.connect(client_id);

    hal_mutex_unlock(s_mqtt_mutex);
    return ok;
}

bool hal_mqtt_connect_auth(const char *client_id, const char *user, const char *pass) {
    if (!validate_non_empty(client_id, "hal_mqtt_connect_auth", "client_id")) {
        return false;
    }
    if (!validate_non_empty(user, "hal_mqtt_connect_auth", "user")) {
        return false;
    }

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    if (!s_server_configured) {
        hal_mutex_unlock(s_mqtt_mutex);
        hal_derr("hal_mqtt_connect_auth: server is not configured");
        return false;
    }

    mqtt_bind_callback();
    const bool ok = s_client.connect(client_id, user, pass);

    hal_mutex_unlock(s_mqtt_mutex);
    return ok;
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

bool hal_mqtt_loop(void) {
    char topic_copy[HAL_MQTT_TOPIC_BUF_SIZE] = {0};
    uint8_t payload_copy[HAL_MQTT_PAYLOAD_BUF_SIZE] = {0};
    uint16_t payload_len = 0;
    bool has_message = false;

    hal_mqtt_message_callback_t callback = NULL;
    void *callback_user = NULL;

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    mqtt_bind_callback();
    const bool ok = s_client.loop();

    if (s_pending_valid) {
        snprintf(topic_copy, sizeof(topic_copy), "%s", s_pending_topic);
        payload_len = s_pending_length;
        if (payload_len > 0u) {
            memcpy(payload_copy, s_pending_payload, payload_len);
        }

        callback = s_user_callback;
        callback_user = s_user_callback_user;

        s_pending_valid = false;
        s_pending_length = 0u;
        has_message = true;
    }

    hal_mutex_unlock(s_mqtt_mutex);

    if (has_message && callback) {
        callback(topic_copy, payload_copy, payload_len, callback_user);
    }

    return ok;
}

bool hal_mqtt_publish(const char *topic, const uint8_t *payload, uint16_t payload_len, bool retained) {
    if (!validate_non_empty(topic, "hal_mqtt_publish", "topic")) {
        return false;
    }
    if (payload_len > 0u && payload == NULL) {
        hal_derr("hal_mqtt_publish: payload is NULL while payload_len > 0");
        return false;
    }

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    const bool ok = s_client.publish(topic, payload, payload_len, retained);

    hal_mutex_unlock(s_mqtt_mutex);
    return ok;
}

bool hal_mqtt_publish_str(const char *topic, const char *payload, bool retained) {
    if (!validate_non_empty(topic, "hal_mqtt_publish_str", "topic")) {
        return false;
    }
    if (!payload) {
        hal_derr("hal_mqtt_publish_str: payload is NULL");
        return false;
    }

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    const bool ok = s_client.publish(topic, payload, retained);

    hal_mutex_unlock(s_mqtt_mutex);
    return ok;
}

bool hal_mqtt_subscribe(const char *topic, uint8_t qos) {
    if (!validate_non_empty(topic, "hal_mqtt_subscribe", "topic")) {
        return false;
    }
    if (qos > 1u) {
        hal_derr("hal_mqtt_subscribe: qos must be 0 or 1");
        return false;
    }

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    const bool ok = s_client.subscribe(topic, qos);

    hal_mutex_unlock(s_mqtt_mutex);
    return ok;
}

bool hal_mqtt_unsubscribe(const char *topic) {
    if (!validate_non_empty(topic, "hal_mqtt_unsubscribe", "topic")) {
        return false;
    }

    mqtt_ensure_mutex();
    hal_mutex_lock(s_mqtt_mutex);

    const bool ok = s_client.unsubscribe(topic);

    hal_mutex_unlock(s_mqtt_mutex);
    return ok;
}

#endif /* HAL_ENABLE_MQTT */

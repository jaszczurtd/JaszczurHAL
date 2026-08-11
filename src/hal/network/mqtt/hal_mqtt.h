#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_MQTT

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_mqtt.h
 * @brief Thread-safe MQTT client wrapper based on PubSubClient.
 *
 * This module is opt-in and is compiled only when HAL_ENABLE_MQTT is defined.
 * Operations that access the transport report HAL_EUNSUPPORTED, HAL_EUNINIT
 * or HAL_EHW when the RP CYW43 board hardware is respectively absent,
 * inactive or known to have failed. Configuration setters remain available
 * before network initialization.
 */

#include "hal/core/hal_status.h"
#ifdef HAL_ENABLE_TLS
#include "hal/network/tls/hal_tls.h"
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Message callback invoked from hal_mqtt_loop().
 *
 * The callback is dispatched after internal locks are released.
 * Topic and payload pointers are valid only for the duration of the callback.
 */
typedef void (*hal_mqtt_message_callback_t)(const char *topic,
                                            const uint8_t *payload,
                                            uint16_t length, void *user);

hal_status_t hal_mqtt_set_server_ex(const char *host, uint16_t port);
hal_status_t hal_mqtt_set_callback_ex(hal_mqtt_message_callback_t callback,
                                      void *user);
hal_status_t hal_mqtt_set_keepalive_ex(uint16_t keepalive_s);
hal_status_t hal_mqtt_set_socket_timeout_ex(uint16_t timeout_s);
hal_status_t hal_mqtt_set_buffer_size_ex(uint16_t size);
#ifdef HAL_ENABLE_TLS
/** Enable MQTTS for subsequent connections. Referenced security buffers must
 * remain valid until TLS is disabled or MQTT is disconnected. */
hal_status_t
hal_mqtt_configure_tls_ex(const hal_tls_security_config_t *security);
/** Return subsequent connections to the existing plaintext MQTT transport. */
hal_status_t hal_mqtt_disable_tls_ex(void);
#endif
hal_status_t hal_mqtt_connect_ex(const char *client_id);
hal_status_t hal_mqtt_connect_auth_ex(const char *client_id, const char *user,
                                      const char *pass);
hal_status_t hal_mqtt_loop_ex(void);
hal_status_t hal_mqtt_publish_ex(const char *topic, const uint8_t *payload,
                                 uint16_t payload_len, bool retained);
hal_status_t hal_mqtt_publish_str_ex(const char *topic, const char *payload,
                                     bool retained);
hal_status_t hal_mqtt_subscribe_ex(const char *topic, uint8_t qos);
hal_status_t hal_mqtt_unsubscribe_ex(const char *topic);

/** @brief Configure MQTT broker hostname/address and port. */
bool hal_mqtt_set_server(const char *host, uint16_t port);

/** @brief Register message callback and opaque user pointer (both may be NULL).
 */
bool hal_mqtt_set_callback(hal_mqtt_message_callback_t callback, void *user);

/** @brief Override keep-alive interval in seconds. */
bool hal_mqtt_set_keepalive(uint16_t keepalive_s);

/** @brief Override socket timeout in seconds. */
bool hal_mqtt_set_socket_timeout(uint16_t timeout_s);

/** @brief Set internal PubSubClient packet buffer size (bytes). */
bool hal_mqtt_set_buffer_size(uint16_t size);

/** @brief Return current PubSubClient packet buffer size (bytes). */
uint16_t hal_mqtt_get_buffer_size(void);

/** @brief Connect using client ID only. */
bool hal_mqtt_connect(const char *client_id);

/** @brief Connect using client ID plus username/password credentials. */
bool hal_mqtt_connect_auth(const char *client_id, const char *user,
                           const char *pass);

/** @brief Disconnect if connected. */
void hal_mqtt_disconnect(void);

/** @brief Return true when MQTT session is connected. */
bool hal_mqtt_connected(void);

/** @brief Return PubSubClient state code. */
int hal_mqtt_state(void);

/**
 * @brief Drive MQTT state machine.
 *
 * Call regularly from the main loop. If an inbound publish arrives, the
 * configured callback is invoked outside the module mutex.
 */
bool hal_mqtt_loop(void);

/** @brief Publish raw payload bytes. */
bool hal_mqtt_publish(const char *topic, const uint8_t *payload,
                      uint16_t payload_len, bool retained);

/** @brief Publish a null-terminated string payload. */
bool hal_mqtt_publish_str(const char *topic, const char *payload,
                          bool retained);

/** @brief Subscribe to topic with qos 0 or 1. */
bool hal_mqtt_subscribe(const char *topic, uint8_t qos);

/** @brief Unsubscribe from topic. */
bool hal_mqtt_unsubscribe(const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_MQTT */

#include <hal/core/hal_app.h>
#include <hal/network/hal_wifi.h>
#include <hal/network/mqtt/hal_mqtt.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <stdio.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

static const char *MQTT_HOST = "broker.hivemq.com";
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_CLIENT_ID = "jaszczurhal-mqtt-example";
static const char *MQTT_TOPIC_PUB = "jaszczurhal/example/telemetry";
static const char *MQTT_TOPIC_SUB = "jaszczurhal/example/cmd";

static uint32_t last_wifi_check_ms = 0;
static uint32_t last_mqtt_connect_ms = 0;
static uint32_t last_publish_ms = 0;
static uint32_t publish_counter = 0;

static void onMqttMessage(const char *topic, const uint8_t *payload,
                          uint16_t length, void *user) {
  (void)user;

  char text[96] = {};
  const uint16_t copy_len =
      length < (sizeof(text) - 1u) ? length : (uint16_t)(sizeof(text) - 1u);
  for (uint16_t i = 0; i < copy_len; ++i) {
    text[i] = (char)payload[i];
  }

  deb("MQTT RX %s: %s", topic ? topic : "", text);
}

static void connectWifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_wifi_check_ms < 5000u) {
    return;
  }
  last_wifi_check_ms = now;

  deb("WiFi: connecting to %s", WIFI_SSID);
  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_set_hostname("jaszczurhal-mqtt");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

static void connectMqtt(void) {
  if (!hal_wifi_is_connected() || hal_mqtt_connected()) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_mqtt_connect_ms < 5000u) {
    return;
  }
  last_mqtt_connect_ms = now;

  deb("MQTT: connecting to %s:%u", MQTT_HOST, MQTT_PORT);
  if (hal_mqtt_connect(MQTT_CLIENT_ID)) {
    deb("MQTT: connected");
    hal_mqtt_subscribe(MQTT_TOPIC_SUB, 0);
  } else {
    derr("MQTT: connect failed, state=%d", hal_mqtt_state());
  }
}

static void publishTelemetry(void) {
  if (!hal_mqtt_connected()) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_publish_ms < 5000u) {
    return;
  }
  last_publish_ms = now;

  char payload[80] = {};
  snprintf(payload, sizeof(payload), "{\"counter\":%lu,\"rssi\":%ld}",
           (unsigned long)publish_counter++, (long)hal_wifi_rssi());

  hal_mqtt_publish_str(MQTT_TOPIC_PUB, payload, false);
  deb("MQTT TX %s: %s", MQTT_TOPIC_PUB, payload);
}

void app_start(void) {
  hal_debug_init_default();
  hal_mqtt_set_server(MQTT_HOST, MQTT_PORT);
  hal_mqtt_set_callback(onMqttMessage, NULL);
  hal_mqtt_set_keepalive(30);
}

void app_task0(void) {
  connectWifi();
  connectMqtt();

  if (hal_mqtt_connected()) {
    hal_mqtt_loop();
    publishTelemetry();
  }
}

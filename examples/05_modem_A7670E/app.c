#ifdef HAL_ENABLE_A7670

#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_modem_at.h>
#include <hal/hal_simcom_a76xx.h>
#include <hal/hal_system.h>
#include <hal/hal_uart.h>
#include <string.h>
#include <tools_c.h>

static const uint8_t PIN_MODEM_TX = 4;
static const uint8_t PIN_MODEM_RX = 5;
static const uint8_t PIN_MODEM_PWR = 6;
static const uint32_t MODEM_BAUD_RATE = 115200;
static const uint32_t MODEM_WARMUP_MS = 15000;
static const uint32_t MODEM_PWR_PULSE_MS = 1500;

static const char *APN = "internet";
static const char *MQTT_BROKER_HOST = "192.168.1.10";
static const uint16_t MQTT_BROKER_PORT = 8883;
static const char *MQTT_CLIENT_ID = "jhal-example";
static const char *MQTT_USER = "user";
static const char *MQTT_PASSWORD = "password";
static const char *SSL_CA_CERT = "ca.pem";
static const char *MQTT_TOPIC_CMD = "dpf/cmd";
static const char *MQTT_TOPIC_DATA = "dpf/data";

static char modem_rx_buf[1024];
static hal_uart_t modem_serial = NULL;
static hal_simcom_a76xx_t modem = NULL;
static volatile bool pending_modem_reset = false;
static uint32_t last_publish_ms = 0;

static void modemTick(void *ctx) {
  (void)ctx;
  hal_watchdog_feed();
}

static void onMqttMessage(int client_index, const char *topic,
                          const uint8_t *payload, size_t payload_len,
                          void *user) {
  (void)client_index;
  (void)user;

  if ((topic == NULL) || (payload == NULL)) {
    return;
  }

  if ((strcmp(topic, MQTT_TOPIC_CMD) == 0) &&
      (payload_len == strlen("modem_reset")) &&
      (memcmp(payload, "modem_reset", payload_len) == 0)) {
    pending_modem_reset = true;
  }
}

static void modemPowerInit(void) {
  hal_gpio_set_mode(PIN_MODEM_PWR, HAL_GPIO_OUTPUT);
  hal_gpio_write(PIN_MODEM_PWR, true);
}

static bool modemPowerCycle(void) {
  if (modem == NULL) {
    return false;
  }

  if (hal_simcom_a76xx_power_toggle(modem, MODEM_PWR_PULSE_MS) !=
      HAL_SIMCOM_A76XX_OK) {
    return false;
  }

  hal_modem_at_sleep_ms(hal_simcom_a76xx_get_at(modem), MODEM_WARMUP_MS);
  return true;
}

static bool modemInit(void) {
  if (modem_serial == NULL) {
    modem_serial = hal_uart_create(HAL_UART_PORT_2, PIN_MODEM_RX, PIN_MODEM_TX);
  }
  if (modem_serial == NULL) {
    return false;
  }

  hal_uart_set_tx(modem_serial, PIN_MODEM_TX);
  hal_uart_set_rx(modem_serial, PIN_MODEM_RX);
  hal_uart_begin(modem_serial, MODEM_BAUD_RATE, SERIAL_8N1);

  if (modem == NULL) {
    hal_simcom_a76xx_config_t cfg = {};
    cfg.uart = modem_serial;
    cfg.pwr_pin = PIN_MODEM_PWR;
    cfg.rx_buf = modem_rx_buf;
    cfg.rx_buf_size = sizeof(modem_rx_buf);
    cfg.default_at_timeout_ms = 3000;
    modem = hal_simcom_a76xx_create(&cfg);
    if (modem == NULL) {
      return false;
    }

    hal_modem_at_set_tick_callback(hal_simcom_a76xx_get_at(modem), modemTick,
                                   NULL);
    hal_simcom_a76xx_mqtt_set_message_callback(modem, onMqttMessage, NULL);
  }

  if (hal_simcom_a76xx_init(modem) != HAL_SIMCOM_A76XX_OK) {
    return false;
  }
  if (hal_simcom_a76xx_wait_sim_ready(modem, 5000) != HAL_SIMCOM_A76XX_OK) {
    return false;
  }
  if (hal_simcom_a76xx_wait_network_registered(modem, 60000) !=
      HAL_SIMCOM_A76XX_OK) {
    return false;
  }

  hal_simcom_a76xx_apn_t apn_cfg = {};
  apn_cfg.apn = APN;
  if (hal_simcom_a76xx_attach_pdp(modem, &apn_cfg) != HAL_SIMCOM_A76XX_OK) {
    return false;
  }

  return true;
}

static bool mqttConnect(void) {
  hal_simcom_a76xx_mqtt_config_t mq = {};
  mq.broker_host = MQTT_BROKER_HOST;
  mq.broker_port = MQTT_BROKER_PORT;
  mq.client_id = MQTT_CLIENT_ID;
  mq.username = MQTT_USER;
  mq.password = MQTT_PASSWORD;
  mq.keepalive_s = 60;
  mq.clean_session = true;
  mq.client_index = 0;
  mq.ssl.enabled = true;
  mq.ssl.ssl_context_id = 0;
  mq.ssl.ca_cert_name = SSL_CA_CERT;
  mq.ssl.ignore_local_time = true;
  mq.ssl.enable_sni = false;
  mq.ssl.sslversion = 4;
  mq.ssl.authmode = 1;

  if (hal_simcom_a76xx_mqtt_connect(modem, &mq) != HAL_SIMCOM_A76XX_OK) {
    return false;
  }
  if (hal_simcom_a76xx_mqtt_subscribe(modem, 0, MQTT_TOPIC_CMD, 1) !=
      HAL_SIMCOM_A76XX_OK) {
    return false;
  }

  return true;
}

static bool mqttPublish(const char *topic, const char *payload) {
  return (hal_simcom_a76xx_mqtt_publish(modem, 0, topic, payload,
                                        strlen(payload),
                                        1) == HAL_SIMCOM_A76XX_OK);
}

void app_start(void) {
  debugInit();
  modemPowerInit();

  if (!modemInit()) {
    derr("modem init failed");
    return;
  }
  if (!mqttConnect()) {
    derr("mqtt connect failed");
    return;
  }
}

void app_task0(void) {
  const uint32_t now = hal_millis();

  if (modem != NULL) {
    hal_simcom_a76xx_mqtt_poll(modem);
  }

  if (pending_modem_reset) {
    pending_modem_reset = false;
    (void)modemPowerCycle();
    (void)modemInit();
    (void)mqttConnect();
  }

  if ((modem != NULL) && (now - last_publish_ms >= 10000u)) {
    last_publish_ms = now;
    (void)mqttPublish(MQTT_TOPIC_DATA, "{\"hello\":\"world\"}");
  }
}

#else /* HAL_ENABLE_A7670 not defined */

#include <hal/hal_app.h>
#include <tools_c.h>

void app_start(void) { derr("Enable HAL_ENABLE_A7670 to build this example"); }

void app_task0(void) {}

#endif

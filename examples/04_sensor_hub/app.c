#include <hal/hal_app.h>
#include <hal/hal_bh1750.h>
#include <hal/hal_dht.h>
#include <hal/hal_ds18b20.h>
#include <hal/hal_i2c.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#if HAL_TARGET_IS_RP
#define SENSOR_I2C_SDA_PIN 4u
#define SENSOR_I2C_SCL_PIN 5u
#define SENSOR_DHT_PIN 14u
#define SENSOR_DS18B20_PIN 16u
#else
#define SENSOR_I2C_SDA_PIN 25u
#define SENSOR_I2C_SCL_PIN 24u
#define SENSOR_DHT_PIN 8u
#define SENSOR_DS18B20_PIN 16u
#endif

#define BH1750_PERIOD_MS 1000u
#define DHT_PERIOD_MS 2000u
#define DS18B20_RETRY_MS 1000u

static hal_bh1750_t s_bh1750;
static bool s_bh1750_ready = false;
static hal_dht_t s_dht = NULL;
static hal_ds18b20_t s_ds18b20 = NULL;
static uint32_t s_last_bh1750_ms = 0u;
static uint32_t s_last_dht_ms = 0u;
static uint32_t s_last_ds18b20_request_ms = 0u;

static void init_bh1750(void) {
  hal_bh1750_config_t cfg = hal_bh1750_default_config();
  cfg.i2c_addr = HAL_BH1750_I2C_ADDR_LOW;

  const hal_status_t status = hal_bh1750_init_ex(&s_bh1750, &cfg);
  s_bh1750_ready = status == HAL_OK;
  if (!s_bh1750_ready) {
    derr("BH1750 unavailable: %s", hal_status_to_string(status));
  }
}

static void init_dht(void) {
  hal_dht_config_t cfg = hal_dht_default_config(SENSOR_DHT_PIN);
  cfg.sensor = HAL_DHT_SENSOR_DHT11;

  const hal_status_t status = hal_dht_init_ex(&cfg, &s_dht);
  if (status != HAL_OK) {
    derr("DHT unavailable: %s", hal_status_to_string(status));
  }
}

static void request_ds18b20(uint32_t now) {
  if (s_ds18b20 == NULL || hal_ds18b20_is_busy(s_ds18b20) ||
      (uint32_t)(now - s_last_ds18b20_request_ms) < DS18B20_RETRY_MS) {
    return;
  }

  s_last_ds18b20_request_ms = now;
  const hal_status_t status = hal_ds18b20_request_ex(s_ds18b20);
  if (status != HAL_OK) {
    derr("DS18B20 conversion failed: %s", hal_status_to_string(status));
  }
}

static void init_ds18b20(void) {
  hal_ds18b20_config_t cfg = {0};
  cfg.data_pin = SENSOR_DS18B20_PIN;
  cfg.resolution_hint = HAL_DS18B20_RES_12_BIT;

  const hal_status_t status = hal_ds18b20_init_ex(&cfg, &s_ds18b20);
  if (status != HAL_OK) {
    derr("DS18B20 unavailable: %s", hal_status_to_string(status));
    return;
  }

  s_last_ds18b20_request_ms = hal_millis() - DS18B20_RETRY_MS;
  request_ds18b20(hal_millis());
}

static void service_bh1750(uint32_t now) {
  if (!s_bh1750_ready ||
      (uint32_t)(now - s_last_bh1750_ms) < BH1750_PERIOD_MS) {
    return;
  }
  s_last_bh1750_ms = now;

  float lux = 0.0f;
  const hal_status_t status = hal_bh1750_light_ex(&s_bh1750, &lux);
  if (status == HAL_OK) {
    deb("BH1750: %.2f lx", (double)lux);
  } else {
    derr("BH1750 read failed: %s", hal_status_to_string(status));
  }
}

static void service_dht(uint32_t now) {
  if (s_dht == NULL || (uint32_t)(now - s_last_dht_ms) < DHT_PERIOD_MS) {
    return;
  }
  s_last_dht_ms = now;

  hal_status_t status = hal_dht_read_ex(s_dht);
  if (status != HAL_OK) {
    derr("DHT read failed: %s", hal_status_to_string(status));
    return;
  }

  hal_dht_sample_t sample = {0};
  status = hal_dht_get_sample_ex(s_dht, &sample);
  if (status == HAL_OK) {
    deb("DHT: %.1f %%RH, %.1f C", (double)sample.humidity,
        (double)sample.temperature_c);
  } else {
    derr("DHT sample failed: %s", hal_status_to_string(status));
  }
}

static void service_ds18b20(uint32_t now) {
  if (s_ds18b20 == NULL) {
    return;
  }

  const hal_status_t poll_status = hal_ds18b20_poll(s_ds18b20);
  if (poll_status != HAL_OK && poll_status != HAL_EAGAIN &&
      poll_status != HAL_ESTATE) {
    derr("DS18B20 poll failed: %s", hal_status_to_string(poll_status));
  }

  float temperature_c = 0.0f;
  bool fresh = false;
  if (hal_ds18b20_take_latest_ex(s_ds18b20, &temperature_c, &fresh) == HAL_OK &&
      fresh) {
    deb("DS18B20: %.2f C", (double)temperature_c);
  }
  request_ds18b20(now);
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL sensor hub ===");

  const hal_status_t i2c_status = hal_i2c_init(
      SENSOR_I2C_SDA_PIN, SENSOR_I2C_SCL_PIN, HAL_I2C_CLOCK_STANDARD_HZ);
  if (i2c_status != HAL_OK) {
    derr("Sensor I2C init failed: %s", hal_status_to_string(i2c_status));
  }
  init_bh1750();
  init_dht();
  init_ds18b20();
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  service_bh1750(now);
  service_dht(now);
  service_ds18b20(now);
  hal_delay_ms(20u);
}

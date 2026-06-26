/**
 * @file app.c
 * @brief Portable DHT11/DHT22 temperature and humidity example over HAL GPIO.
 */

#include <hal/hal_app.h>
#include <hal/hal_dht.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_DHT_PIN 14u
#else
/* STM32 pin id = port * 16 + pin: PA8. */
#define EXAMPLE_DHT_PIN 8u
#endif

#ifndef EXAMPLE_DHT_SENSOR
#define EXAMPLE_DHT_SENSOR HAL_DHT_SENSOR_DHT11
#endif

static hal_dht_t s_dht = NULL;
static uint32_t s_last_read_ms = 0u;

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL DHT temperature/humidity ===");
  deb("DHT data pin: %u", (unsigned)EXAMPLE_DHT_PIN);

  hal_dht_config_t cfg = hal_dht_default_config(EXAMPLE_DHT_PIN);
  cfg.sensor = EXAMPLE_DHT_SENSOR;

  s_dht = hal_dht_init(&cfg);
  if (s_dht == NULL) {
    derr("DHT init FAILED");
  }
}

void app_task0(void) {
  if (s_dht == NULL) {
    hal_delay_ms(1000u);
    return;
  }

  const uint32_t now = hal_millis();
  if ((now - s_last_read_ms) < 2000u) {
    return;
  }
  s_last_read_ms = now;

  if (!hal_dht_read(s_dht)) {
    derr("DHT read FAILED");
    return;
  }

  hal_dht_sample_t sample = {};
  if (hal_dht_get_sample(s_dht, &sample)) {
    deb("Humidity: %.1f %%  Temperature: %.1f C / %.1f F",
        (double)sample.humidity, (double)sample.temperature_c,
        (double)sample.temperature_f);
  }
}

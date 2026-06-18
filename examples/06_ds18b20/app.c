#include <hal/hal_app.h>
#include <hal/hal_ds18b20.h>
#include <hal/hal_system.h>
#include <tools_c.h>

static const uint8_t DS18B20_DATA_PIN = 16;
static hal_ds18b20_t sensor = NULL;

static void startConversion(void) {
  if (sensor && !hal_ds18b20_is_busy(sensor)) {
    (void)hal_ds18b20_request(sensor);
  }
}

void app_start(void) {
  debugInit();

  hal_ds18b20_config_t cfg = {};
  cfg.data_pin = DS18B20_DATA_PIN;
  cfg.use_rom = false;
  cfg.resolution_hint = HAL_DS18B20_RES_12_BIT;

  sensor = hal_ds18b20_init(&cfg);
  if (!sensor) {
    derr("DS18B20 not found on pin %u", DS18B20_DATA_PIN);
    return;
  }

  startConversion();
}

void app_task0(void) {
  if (!sensor) {
    hal_delay_ms(1000);
    return;
  }

  hal_ds18b20_poll(sensor);

  float temp_c = 0.0f;
  bool fresh = false;
  if (hal_ds18b20_take_latest(sensor, &temp_c, &fresh) && fresh) {
    deb("DS18B20: %.2f C", temp_c);
    startConversion();
  }

  hal_delay_ms(20);
}

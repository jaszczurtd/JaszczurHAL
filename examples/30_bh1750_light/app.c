/**
 * @file app.c
 * @brief Portable BH1750 ambient-light sensor example over HAL I2C.
 */

#include <hal/hal_app.h>
#include <hal/hal_bh1750.h>
#include <hal/hal_i2c.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#include <stdint.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_I2C_SDA_PIN 4u
#define EXAMPLE_I2C_SCL_PIN 5u
#else
#define EXAMPLE_I2C_SDA_PIN 25u
#define EXAMPLE_I2C_SCL_PIN 24u
#endif

static hal_bh1750_t s_bh1750;
static bool s_bh1750_ready = false;

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL BH1750 light sensor ===");
  deb("BH1750: I2C address 0x23, continuous H-resolution mode");

  hal_i2c_init(EXAMPLE_I2C_SDA_PIN, EXAMPLE_I2C_SCL_PIN,
               HAL_I2C_CLOCK_STANDARD_HZ);

  hal_bh1750_config_t cfg = hal_bh1750_default_config();
  cfg.i2c_addr = HAL_BH1750_I2C_ADDR_LOW;

  s_bh1750_ready = hal_bh1750_init(&s_bh1750, &cfg);
  if (!s_bh1750_ready) {
    derr("BH1750 init FAILED");
  }
}

void app_task0(void) {
  if (!s_bh1750_ready) {
    hal_delay_ms(1000u);
    return;
  }

  const float lux = hal_bh1750_light(&s_bh1750);
  if (lux < 0.0f) {
    derr("BH1750 read FAILED");
  } else {
    deb("Light: %.2f lx", (double)lux);
  }

  hal_delay_ms(1000u);
}

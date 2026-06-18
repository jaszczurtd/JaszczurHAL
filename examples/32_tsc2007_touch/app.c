/**
 * @file app.c
 * @brief Portable TSC2007 resistive touch controller example over HAL I2C.
 */

#include <hal/hal_app.h>
#include <hal/hal_i2c.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <hal/hal_tsc2007.h>
#include <tools_c.h>

#include <stdint.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_I2C_SDA_PIN 4u
#define EXAMPLE_I2C_SCL_PIN 5u
#else
/* STM32 pin id = port * 16 + pin: PB9/PB8. */
#define EXAMPLE_I2C_SDA_PIN 25u
#define EXAMPLE_I2C_SCL_PIN 24u
#endif

static hal_tsc2007_t s_touch;
static bool s_touch_ready = false;
static uint32_t s_last_read_ms = 0u;

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL TSC2007 touch controller ===");
  deb("TSC2007: I2C address 0x%02X", HAL_TSC2007_I2C_ADDR_DEFAULT);

  hal_i2c_init_bus(0u, EXAMPLE_I2C_SDA_PIN, EXAMPLE_I2C_SCL_PIN,
                   HAL_I2C_CLOCK_STANDARD_HZ);

  hal_tsc2007_config_t cfg = hal_tsc2007_default_config();
  cfg.i2c_bus = 0u;
  cfg.i2c_addr = HAL_TSC2007_I2C_ADDR_DEFAULT;

  s_touch_ready = hal_tsc2007_init(&s_touch, &cfg);
  if (!s_touch_ready) {
    derr("TSC2007 init FAILED");
    return;
  }

  deb("TSC2007 ready");
}

void app_task0(void) {
  if (!s_touch_ready) {
    hal_delay_ms(1000u);
    return;
  }

  const uint32_t now = hal_millis();
  if ((now - s_last_read_ms) < 100u) {
    return;
  }
  s_last_read_ms = now;

  uint16_t x = 0u;
  uint16_t y = 0u;
  uint16_t z1 = 0u;
  uint16_t z2 = 0u;
  if (hal_tsc2007_read_touch(&s_touch, &x, &y, &z1, &z2)) {
    deb("Touch: x=%u y=%u z1=%u z2=%u", (unsigned)x, (unsigned)y, (unsigned)z1,
        (unsigned)z2);
  }
}

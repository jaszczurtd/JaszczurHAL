#include <hal/hal_app.h>
#include <hal/hal_i2c.h>
#include <hal/hal_stmpe610.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#include <stdint.h>

static hal_stmpe610_t touch;

#if HAL_TARGET_IS_RP2040
static const uint8_t I2C_SDA_PIN = 4u;
static const uint8_t I2C_SCL_PIN = 5u;
#elif HAL_TARGET_IS_STM32G474
static const uint8_t I2C_SDA_PIN = 25u; /* PB9 */
static const uint8_t I2C_SCL_PIN = 24u; /* PB8 */
#else
static const uint8_t I2C_SDA_PIN = 4u;
static const uint8_t I2C_SCL_PIN = 5u;
#endif

void app_start(void) {
  debugInit();

  hal_i2c_init_bus(0u, I2C_SDA_PIN, I2C_SCL_PIN, HAL_I2C_CLOCK_STANDARD_HZ);

  const hal_stmpe610_config_t cfg =
      hal_stmpe610_i2c_config(0u, HAL_STMPE610_I2C_ADDR_DEFAULT);

  if (!hal_stmpe610_init(&touch, &cfg)) {
    derr("STMPE610 init failed\r\n");
    return;
  }

  deb("STMPE610 touch controller ready\r\n");
}

void app_task0(void) {
  if (hal_stmpe610_touched(&touch)) {
    const hal_stmpe610_point_t point = hal_stmpe610_get_point(&touch);
    deb("touch x=%d y=%d z=%d\r\n", point.x, point.y, point.z);
  }

  hal_delay_ms(100u);
}

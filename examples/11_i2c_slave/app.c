#include <hal/hal_app.h>
#include <hal/hal_i2c_slave.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#if HAL_TARGET_IS_STM32G474
#define EXAMPLE_PIN(port, pin) ((uint8_t)(((port) * 16u) + (pin)))
#define I2C_SLAVE_SDA_PIN EXAMPLE_PIN(1u, 9u) /* PB9 = I2C1_SDA */
#define I2C_SLAVE_SCL_PIN EXAMPLE_PIN(1u, 8u) /* PB8 = I2C1_SCL */
#else
#define I2C_SLAVE_SDA_PIN 4u
#define I2C_SLAVE_SCL_PIN 5u
#endif

#define I2C_SLAVE_ADDR 0x42u

static const uint8_t REG_STATUS = 0;
static const uint8_t REG_COUNTER = 1;
static const uint8_t REG_MILLIS_HI = 2;
static const uint8_t REG_MILLIS_LO = 4;

static uint32_t last_update_ms = 0;
static uint16_t counter = 0;

void app_start(void) {
  debugInit();

  hal_i2c_slave_init(I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN, I2C_SLAVE_ADDR);
  hal_i2c_slave_reg_write8(REG_STATUS, 0xA5);
  deb("I2C slave ready at 0x%02X", I2C_SLAVE_ADDR);
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if (now - last_update_ms < 1000u) {
    return;
  }
  last_update_ms = now;

  hal_i2c_slave_reg_write16(REG_COUNTER, counter++);
  hal_i2c_slave_reg_write16(REG_MILLIS_HI, (uint16_t)(now >> 16));
  hal_i2c_slave_reg_write16(REG_MILLIS_LO, (uint16_t)(now & 0xFFFFu));

  deb("I2C slave transactions=%lu",
      (unsigned long)hal_i2c_slave_get_transaction_count());
}

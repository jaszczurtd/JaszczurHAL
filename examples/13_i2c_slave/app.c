#include <hal/hal_app.h>
#include <hal/hal_i2c_slave.h>
#include <hal/hal_serial.h>
#include <hal/hal_system.h>

static const uint8_t I2C_SLAVE_SDA_PIN = 4;
static const uint8_t I2C_SLAVE_SCL_PIN = 5;
static const uint8_t I2C_SLAVE_ADDR = 0x42;

static const uint8_t REG_STATUS = 0;
static const uint8_t REG_COUNTER = 1;
static const uint8_t REG_MILLIS_HI = 2;
static const uint8_t REG_MILLIS_LO = 4;

static uint32_t last_update_ms = 0;
static uint16_t counter = 0;

void app_start(void) {
    hal_debug_init(115200, NULL);

    hal_i2c_slave_init(I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN, I2C_SLAVE_ADDR);
    hal_i2c_slave_reg_write8(REG_STATUS, 0xA5);
    hal_deb("I2C slave ready at 0x%02X", I2C_SLAVE_ADDR);
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

    hal_deb("I2C slave transactions=%lu",
            (unsigned long)hal_i2c_slave_get_transaction_count());
}

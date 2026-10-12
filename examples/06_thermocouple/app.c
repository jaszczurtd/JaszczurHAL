/*
 * Print temperatures from MCP9600 and MAX6675 thermocouple interfaces.
 * The devices are initialized independently and read through the same HAL API.
 */

#include <hal/core/hal_app.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <hal/temperature/hal_thermocouple.h>

#if defined(HAL_ENABLE_MCP9600)
static hal_thermocouple_t mcp9600 = NULL;
#endif

#if defined(HAL_ENABLE_MAX6675)
static hal_thermocouple_t max6675 = NULL;
#endif

static uint32_t last_report_ms = 0;

void app_start(void) {
  hal_debug_init_default();

#if defined(HAL_ENABLE_MCP9600)
  hal_thermocouple_config_t mcp_cfg = {};
  mcp_cfg.chip = HAL_THERMOCOUPLE_CHIP_MCP9600;
  /* Bus, pins and address come from device_tree.json. */
  mcp_cfg.bus.i2c.sda_pin = JH_HW_BUS_I2C_PIN_SDA;
  mcp_cfg.bus.i2c.scl_pin = JH_HW_BUS_I2C_PIN_SCL;
  mcp_cfg.bus.i2c.clock_hz = JH_HW_BUS_I2C_FREQUENCY_HZ;
  mcp_cfg.bus.i2c.i2c_bus = JH_HW_BUS_I2C_INDEX;
  mcp_cfg.bus.i2c.i2c_addr = JH_HW_NODE_MCP9600_ADDRESS;

  mcp9600 = hal_thermocouple_init(&mcp_cfg);
  if (mcp9600) {
    hal_thermocouple_set_type(mcp9600, HAL_THERMOCOUPLE_TYPE_K);
    hal_thermocouple_set_filter(mcp9600, 2);
  } else {
    derr("MCP9600 not found");
  }
#endif

#if defined(HAL_ENABLE_MAX6675)
  hal_thermocouple_config_t max_cfg = {};
  max_cfg.chip = HAL_THERMOCOUPLE_CHIP_MAX6675;
  /* Bit-banged lines from device_tree.json. */
  max_cfg.bus.spi.sclk_pin = JH_HW_NODE_MAX6675_SCLK_PIN_OUT;
  max_cfg.bus.spi.cs_pin = JH_HW_NODE_MAX6675_CS_PIN_OUT;
  max_cfg.bus.spi.miso_pin = JH_HW_NODE_MAX6675_SO_PIN_IN;

  max6675 = hal_thermocouple_init(&max_cfg);
  if (!max6675) {
    derr("MAX6675 not found");
  }
#endif
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if (now - last_report_ms < 1000u) {
    return;
  }
  last_report_ms = now;

#if defined(HAL_ENABLE_MCP9600)
  if (mcp9600) {
    deb("MCP9600: hot=%.2f C ambient=%.2f C", hal_thermocouple_read(mcp9600),
        hal_thermocouple_read_ambient(mcp9600));
  }
#endif

#if defined(HAL_ENABLE_MAX6675)
  if (max6675) {
    deb("MAX6675: hot=%.2f C", hal_thermocouple_read(max6675));
  }
#endif
}

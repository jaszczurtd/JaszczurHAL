#include <hal/hal_app.h>
#include <hal/hal_i2c.h>
#include <hal/hal_system.h>
#include <hal/hal_thermocouple.h>
#include <tools_c.h>

#if defined(HAL_ENABLE_MCP9600)
static hal_thermocouple_t mcp9600 = NULL;
#endif

#if defined(HAL_ENABLE_MAX6675)
static hal_thermocouple_t max6675 = NULL;
#endif

static uint32_t last_report_ms = 0;

void app_start(void) {
  debugInit();

#if defined(HAL_ENABLE_MCP9600)
  hal_thermocouple_config_t mcp_cfg = {0};
  mcp_cfg.chip = HAL_THERMOCOUPLE_CHIP_MCP9600;
  mcp_cfg.bus.i2c.sda_pin = 4;
  mcp_cfg.bus.i2c.scl_pin = 5;
  mcp_cfg.bus.i2c.clock_hz = HAL_I2C_CLOCK_STANDARD_HZ;
  mcp_cfg.bus.i2c.i2c_bus = 0;
  mcp_cfg.bus.i2c.i2c_addr = 0x67;

  mcp9600 = hal_thermocouple_init(&mcp_cfg);
  if (mcp9600) {
    hal_thermocouple_set_type(mcp9600, HAL_THERMOCOUPLE_TYPE_K);
    hal_thermocouple_set_filter(mcp9600, 2);
  } else {
    derr("MCP9600 not found");
  }
#endif

#if defined(HAL_ENABLE_MAX6675)
  hal_thermocouple_config_t max_cfg = {0};
  max_cfg.chip = HAL_THERMOCOUPLE_CHIP_MAX6675;
  max_cfg.bus.spi.sclk_pin = 18;
  max_cfg.bus.spi.cs_pin = 17;
  max_cfg.bus.spi.miso_pin = 16;

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

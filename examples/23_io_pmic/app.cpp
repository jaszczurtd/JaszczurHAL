/**
 * @file app.cpp
 * @brief Combined external I/O, converter, PMIC and RGB LED example.
 */

#include <hal/analog/hal_mcp3221.h>
#include <hal/analog/hal_mcp4725.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/core/hal_target.h>
#include <hal/gpio/hal_hc595.h>
#include <hal/gpio/hal_mcp23017.h>
#include <hal/gpio/hal_pca9654e.h>
#include <hal/gpio/hal_pcf8574.h>
#include <hal/gpio/hal_rgb_led.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/power/hal_adp5360.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>
#include <tools.h>

#if HAL_TARGET_IS_RP
#define EXAMPLE_I2C_SDA 4u
#define EXAMPLE_I2C_SCL 5u
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK 18u
#define EXAMPLE_HC595_CS 17u
#define EXAMPLE_RGB_PIN 22u
#elif HAL_TARGET_IS_STM32G474
/* STM32 pin id = port * 16 + pin: PB9/PB8, SPI1, PB6 and PA8. */
#define EXAMPLE_I2C_SDA 25u
#define EXAMPLE_I2C_SCL 24u
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_HC595_CS 22u
#define EXAMPLE_RGB_PIN 8u
#else
#define EXAMPLE_I2C_SDA 23u
#define EXAMPLE_I2C_SCL 22u
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_HC595_CS 16u
#define EXAMPLE_RGB_PIN 8u
#endif

static hal_mcp23017_t s_mcp23017;
static hal_pca9654e_t s_pca9654e;
static hal_pcf8574_t s_pcf8574;
static hal_hc595_t s_hc595;
static hal_mcp3221_t s_mcp3221;
static hal_mcp4725_t s_mcp4725;
static hal_adp5360_t s_pmic;

static bool s_mcp23017_ready = false;
static bool s_pca9654e_ready = false;
static bool s_pcf8574_ready = false;
static bool s_hc595_ready = false;
static bool s_mcp3221_ready = false;
static bool s_mcp4725_ready = false;
static bool s_pmic_ready = false;
static bool s_rgb_ready = false;
static uint8_t s_step = 0u;

static const hal_rgb_led_color_t COLORS[] = {
    HAL_RGB_LED_RED,    HAL_RGB_LED_GREEN, HAL_RGB_LED_BLUE, HAL_RGB_LED_YELLOW,
    HAL_RGB_LED_PURPLE, HAL_RGB_LED_WHITE, HAL_RGB_LED_NONE,
};

static void log_status(const char *name, hal_status_t status) {
  if (status == HAL_OK) {
    deb("%s ready", name);
  } else {
    derr("%s init failed: %s (%d)", name, hal_status_to_string(status),
         (int)status);
  }
}

static void init_i2c_devices(void) {
  hal_mcp23017_config_t mcp23017_cfg = hal_mcp23017_default_config();
  mcp23017_cfg.i2c_addr = 0x20u;
  hal_status_t status = hal_mcp23017_init_ex(&s_mcp23017, &mcp23017_cfg);
  s_mcp23017_ready = status == HAL_OK;
  log_status("MCP23017", status);

  hal_pca9654e_config_t pca9654e_cfg = hal_pca9654e_default_config();
  pca9654e_cfg.i2c_addr = 0x21u;
  status = hal_pca9654e_init_ex(&s_pca9654e, &pca9654e_cfg);
  s_pca9654e_ready = status == HAL_OK;
  log_status("PCA9654E", status);

  hal_pcf8574_config_t pcf8574_cfg = hal_pcf8574_default_config();
  pcf8574_cfg.i2c_addr = 0x22u;
  status = hal_pcf8574_init_ex(&s_pcf8574, &pcf8574_cfg);
  s_pcf8574_ready = status == HAL_OK;
  log_status("PCF8574", status);

  status = hal_mcp3221_init_ex(&s_mcp3221, nullptr);
  s_mcp3221_ready = status == HAL_OK;
  log_status("MCP3221", status);

  status = hal_mcp4725_init_ex(&s_mcp4725, nullptr);
  s_mcp4725_ready = status == HAL_OK;
  log_status("MCP4725", status);

  hal_adp5360_config_t pmic_cfg = hal_adp5360_default_config();
  pmic_cfg.i2c_addr = HAL_ADP5360_I2C_ADDR_DEFAULT;
  pmic_cfg.charger.i_fast_charge_ua = 10000u;
  pmic_cfg.charger.i_input_limit_ua = 50000u;
  pmic_cfg.fuel_gauge.battery_capacity_mah = 100u;
  status = hal_adp5360_init_ex(&s_pmic, &pmic_cfg);
  s_pmic_ready = status == HAL_OK;
  log_status("ADP5360", status);
  if (s_pmic_ready) {
    status = hal_adp5360_regulator_set_voltage(
        &s_pmic, HAL_ADP5360_REGULATOR_BUCK, 1200000, 1200000);
    log_status("ADP5360 BUCK at 1.2 V", status);
  }
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL external I/O + PMIC example ===");

  hal_i2c_init(EXAMPLE_I2C_SDA, EXAMPLE_I2C_SCL, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);
  init_i2c_devices();

  hal_hc595_config_t hc595_cfg = hal_hc595_default_config(EXAMPLE_HC595_CS);
  hc595_cfg.chips = 1u;
  const hal_status_t status = hal_hc595_init_ex(&s_hc595, &hc595_cfg);
  s_hc595_ready = status == HAL_OK;
  log_status("74HC595", status);

  const hal_status_t rgb_status =
      hal_rgb_led_init_ex(EXAMPLE_RGB_PIN, 1u, HAL_RGB_LED_PIXEL_GRB_KHZ800);
  s_rgb_ready = rgb_status == HAL_OK;
  log_status("RGB LED", rgb_status);
  if (s_rgb_ready) {
    hal_rgb_led_set_brightness(24u);
    deb("RGB LED data pin=%u", (unsigned)EXAMPLE_RGB_PIN);
  }
}

static void exercise_io_devices(bool on, uint8_t pattern) {
  if (s_mcp23017_ready) {
    const hal_status_t status = hal_mcp23017_write_all_ex(&s_mcp23017, pattern);
    if (status != HAL_OK) {
      derr("MCP23017 write failed: %s", hal_status_to_string(status));
    }
  }
  if (s_pca9654e_ready) {
    const hal_status_t status = hal_pca9654e_write_all_ex(&s_pca9654e, pattern);
    if (status != HAL_OK) {
      derr("PCA9654E write failed: %s", hal_status_to_string(status));
    }
  }
  if (s_pcf8574_ready) {
    const hal_status_t status = hal_pcf8574_write_all_ex(&s_pcf8574, pattern);
    if (status != HAL_OK) {
      derr("PCF8574 write failed: %s", hal_status_to_string(status));
    }
  }
  if (s_hc595_ready) {
    const hal_status_t status = hal_hc595_write_all_ex(&s_hc595, pattern);
    if (status != HAL_OK) {
      derr("74HC595 write failed: %s", hal_status_to_string(status));
    }
  }
  if (s_mcp3221_ready) {
    uint16_t sample = 0u;
    const hal_status_t status = hal_mcp3221_read_ex(&s_mcp3221, &sample);
    if (status == HAL_OK) {
      deb("MCP3221 raw=%u", (unsigned)sample);
    } else {
      derr("MCP3221 read failed: %s", hal_status_to_string(status));
    }
  }
  if (s_mcp4725_ready) {
    const uint16_t dac = on ? 3072u : 1024u;
    const hal_status_t status = hal_mcp4725_write_ex(&s_mcp4725, dac);
    if (status == HAL_OK) {
      deb("MCP4725 raw=%u", (unsigned)dac);
    } else {
      derr("MCP4725 write failed: %s", hal_status_to_string(status));
    }
  }
}

static void report_pmic(void) {
  if (!s_pmic_ready) {
    return;
  }

  uint8_t soc = 0u;
  uint32_t vbat_uv = 0u;
  bool present = false;
  hal_adp5360_charger_status_t charger = HAL_ADP5360_CHARGER_STATUS_UNKNOWN;
  const hal_status_t soc_status =
      hal_adp5360_fuel_gauge_get_soc_pct(&s_pmic, &soc);
  const hal_status_t voltage_status =
      hal_adp5360_fuel_gauge_get_voltage_uv(&s_pmic, &vbat_uv);
  const hal_status_t present_status =
      hal_adp5360_charger_get_battery_present(&s_pmic, &present);
  const hal_status_t charger_status =
      hal_adp5360_charger_get_status(&s_pmic, &charger);
  if (soc_status == HAL_OK && voltage_status == HAL_OK &&
      present_status == HAL_OK && charger_status == HAL_OK) {
    deb("ADP5360 soc=%u%% vbat=%luuV present=%d charger=%d", (unsigned)soc,
        (unsigned long)vbat_uv, present ? 1 : 0, (int)charger);
  } else {
    derr("ADP5360 telemetry read failed");
  }
}

void app_task0(void) {
  const bool on = (s_step & 1u) != 0u;
  const uint8_t pattern = on ? 0x55u : 0xaau;
  exercise_io_devices(on, pattern);

  const size_t color_count = sizeof(COLORS) / sizeof(COLORS[0]);
  const hal_rgb_led_color_t color = COLORS[s_step % color_count];
  if (s_rgb_ready) {
    const hal_status_t status = hal_rgb_led_set_color(color);
    if (status == HAL_OK) {
      deb("pattern=0x%02X RGB=%u", (unsigned)pattern, (unsigned)color);
    } else {
      derr("RGB LED update failed: %s", hal_status_to_string(status));
      if (status == HAL_EUNINIT) {
        s_rgb_ready = false;
      }
    }
  } else {
    deb("pattern=0x%02X", (unsigned)pattern);
  }

  if ((s_step & 1u) == 0u) {
    report_pmic();
  }
  ++s_step;
  hal_delay_ms(1000u);
}

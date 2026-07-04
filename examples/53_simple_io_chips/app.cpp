/**
 * @file app.cpp
 * @brief Simple external I/O chip example.
 */

#include <hal/hal_app.h>
#include <hal/hal_hc595.h>
#include <hal/hal_i2c.h>
#include <hal/hal_mcp23017.h>
#include <hal/hal_mcp3221.h>
#include <hal/hal_mcp4725.h>
#include <hal/hal_pca9654e.h>
#include <hal/hal_pcf8574.h>
#include <hal/hal_spi.h>
#include <hal/hal_status.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_I2C_SDA 4u
#define EXAMPLE_I2C_SCL 5u
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK 18u
#define EXAMPLE_HC595_CS 17u
#else
/* STM32 pin id = port * 16 + pin: PB7/PB6 I2C1, PA5/PA6/PA7 SPI1, PB0 CS. */
#define EXAMPLE_I2C_SDA 23u
#define EXAMPLE_I2C_SCL 22u
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_HC595_CS 16u
#endif

static hal_mcp23017_t s_mcp23017;
static hal_pca9654e_t s_pca9654e;
static hal_pcf8574_t s_pcf8574;
static hal_hc595_t s_hc595;
static hal_mcp3221_t s_mcp3221;
static hal_mcp4725_t s_mcp4725;

static bool s_mcp23017_ready = false;
static bool s_pca9654e_ready = false;
static bool s_pcf8574_ready = false;
static bool s_hc595_ready = false;
static bool s_mcp3221_ready = false;
static bool s_mcp4725_ready = false;
static uint8_t s_step = 0u;

static void log_status(const char *name, hal_status_t status) {
  if (status == HAL_OK) {
    deb("%s ready", name);
  } else {
    derr("%s init failed: %s (%d)", name, hal_status_to_string(status),
         (int)status);
  }
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL simple I/O chips example ===");

  hal_i2c_init(EXAMPLE_I2C_SDA, EXAMPLE_I2C_SCL, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);

  hal_status_t status = hal_mcp23017_init_ex(&s_mcp23017, NULL);
  s_mcp23017_ready = status == HAL_OK;
  log_status("MCP23017", status);

  status = hal_pca9654e_init_ex(&s_pca9654e, NULL);
  s_pca9654e_ready = status == HAL_OK;
  log_status("PCA9654E", status);

  status = hal_pcf8574_init_ex(&s_pcf8574, NULL);
  s_pcf8574_ready = status == HAL_OK;
  log_status("PCF8574", status);

  hal_hc595_config_t hc595_cfg = hal_hc595_default_config(EXAMPLE_HC595_CS);
  hc595_cfg.chips = 1u;
  status = hal_hc595_init_ex(&s_hc595, &hc595_cfg);
  s_hc595_ready = status == HAL_OK;
  log_status("74HC595", status);

  status = hal_mcp3221_init_ex(&s_mcp3221, NULL);
  s_mcp3221_ready = status == HAL_OK;
  log_status("MCP3221", status);

  status = hal_mcp4725_init_ex(&s_mcp4725, NULL);
  s_mcp4725_ready = status == HAL_OK;
  log_status("MCP4725", status);
}

void app_task0(void) {
  const bool on = (s_step & 1u) != 0u;
  const uint8_t pattern = on ? 0x55u : 0xAAu;

  if (s_mcp23017_ready) {
    hal_status_t status = hal_mcp23017_write_all_ex(&s_mcp23017, pattern);
    if (status != HAL_OK) {
      derr("MCP23017 write failed: %s", hal_status_to_string(status));
    }
  }

  if (s_pca9654e_ready) {
    hal_status_t status = hal_pca9654e_write_all_ex(&s_pca9654e, pattern);
    if (status != HAL_OK) {
      derr("PCA9654E write failed: %s", hal_status_to_string(status));
    }
  }

  if (s_pcf8574_ready) {
    hal_status_t status = hal_pcf8574_write_all_ex(&s_pcf8574, pattern);
    if (status != HAL_OK) {
      derr("PCF8574 write failed: %s", hal_status_to_string(status));
    }
  }

  if (s_hc595_ready) {
    hal_status_t status = hal_hc595_write_all_ex(&s_hc595, pattern);
    if (status != HAL_OK) {
      derr("74HC595 write failed: %s", hal_status_to_string(status));
    }
  }

  if (s_mcp3221_ready) {
    uint16_t sample = 0u;
    hal_status_t status = hal_mcp3221_read_ex(&s_mcp3221, &sample);
    if (status == HAL_OK) {
      deb("MCP3221 raw=%u", (unsigned)sample);
    } else {
      derr("MCP3221 read failed: %s", hal_status_to_string(status));
    }
  }

  if (s_mcp4725_ready) {
    const uint16_t dac = on ? 3072u : 1024u;
    hal_status_t status = hal_mcp4725_write_ex(&s_mcp4725, dac);
    if (status == HAL_OK) {
      deb("MCP4725 raw=%u", (unsigned)dac);
    } else {
      derr("MCP4725 write failed: %s", hal_status_to_string(status));
    }
  }

  deb("pattern=0x%02X", (unsigned)pattern);
  s_step++;
  hal_delay_ms(1000u);
}
